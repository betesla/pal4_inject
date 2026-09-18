#include "update_installer.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#include <thread>
#include <tlhelp32.h>
#include <objbase.h>
#include "nlohmann/json.hpp"

namespace pal4::inject::update {
namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
std::wstring g_ready_event;

struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

std::string Read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("无法读取更新任务文件。");
    return {std::istreambuf_iterator<char>(input), {}};
}

void Write(const fs::path& path, const std::string& text) {
    const auto temporary = fs::path(path.wstring() + L".tmp");
    {
        Handle output{CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        DWORD written = 0;
        if (output.value == INVALID_HANDLE_VALUE ||
            !WriteFile(output.value, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
            written != text.size() || !FlushFileBuffers(output.value))
            throw std::runtime_error("无法写入更新任务文件。");
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("无法保存更新任务文件。");
}

void CheckPath(const fs::path& root, const fs::path& path) {
    auto relative = path.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty() || relative.is_absolute()) throw std::runtime_error("更新路径越界。");
    auto current = root;
    for (const auto& part : relative) {
        if (part == L"..") throw std::runtime_error("更新路径越界。");
        current /= part;
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("更新路径不能包含链接或目录联接。");
    }
}

fs::path RootFromStage(const fs::path& stage) {
    if (!stage.is_absolute() || stage.parent_path().filename() != L".pal4plus-update")
        throw std::runtime_error("更新准备目录无效。");
    const auto root = stage.parent_path().parent_path();
    CheckPath(root, stage);
    return root;
}

void CheckStage(const fs::path& root, const fs::path& stage) {
    if (!fs::equivalent(RootFromStage(stage), root)) throw std::runtime_error("更新安装目录不匹配。");
}

PROCESS_INFORMATION Start(const fs::path& executable, const std::vector<std::wstring>& args,
    const fs::path& directory, bool hidden = false) {
    auto command = QuoteArgument(executable.wstring());
    for (const auto& arg : args) command += L" " + QuoteArgument(arg);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (hidden) { startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE; }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            hidden ? CREATE_NO_WINDOW : 0, nullptr, directory.c_str(), &startup, &process))
        throw std::runtime_error("无法启动更新进程或新版启动器。");
    return process;
}

HANDLE LockInstallation(const fs::path& root) {
    const auto path = root / L".pal4plus-update/install.lock";
    CheckPath(root, path);
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("另一项更新正在安装，或更新目录不可写。");
    return handle;
}

void Replace(const fs::path& source, const fs::path& target) {
    fs::create_directories(target.parent_path());
    const auto temporary = fs::path(target.wstring() + L".pal4plus-new");
    fs::copy_file(source, temporary, fs::copy_options::overwrite_existing);
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temporary);
        throw std::runtime_error("无法替换程序文件，请退出游戏和其他启动器后重试。");
    }
}

Json ReadJournal(const fs::path& root, const fs::path& stage) {
    CheckStage(root, stage);
    const auto journal = Json::parse(Read(stage / L"journal.json"));
    for (const auto& file : journal.at("files")) {
        const auto path = file.at("path").get<std::string>();
        if (!IsManagedPath(path)) throw std::runtime_error("更新恢复清单包含非托管文件。");
        CheckPath(root, root / fs::path(Wide(path)));
        CheckPath(root, stage / L"backup" / fs::path(Wide(path)));
    }
    return journal;
}

constexpr char kExtractScript[] = R"PS(param([string]$Package,[string]$Destination,[string]$Manifest)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$spec = Get-Content -LiteralPath $Manifest -Raw -Encoding UTF8 | ConvertFrom-Json
$expected = @{}
foreach ($file in $spec.files) { $expected[$file.path] = $file }
$archive = [IO.Compression.ZipFile]::OpenRead($Package)
try {
    $seen = @{}
    foreach ($entry in $archive.Entries) {
        $name = $entry.FullName
        if ($name.EndsWith('/')) {
            if ($name -ne 'pal4_inject/') { throw 'Unexpected ZIP directory' }
            continue
        }
        if (!$expected.ContainsKey($name) -or $seen.ContainsKey($name)) { throw 'Unexpected or duplicate ZIP entry' }
        if ($name.Contains('\') -or $name.Contains(':') -or $name.Contains('..')) { throw 'Invalid ZIP path' }
        if ($entry.Length -ne $expected[$name].size) { throw 'ZIP size mismatch' }
        if (($entry.ExternalAttributes -band 0xA0000000) -eq 0xA0000000) { throw 'ZIP symlink rejected' }
        $seen[$name] = $true
    }
    if ($seen.Count -ne $expected.Count) { throw 'Incomplete update ZIP' }
    foreach ($entry in $archive.Entries) {
        if ($entry.FullName.EndsWith('/')) { continue }
        $target = Join-Path $Destination $entry.FullName
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $false)
    }
} finally { $archive.Dispose() }
)PS";
}  // namespace

std::wstring Wide(const std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!size) throw std::runtime_error("UTF-8 编码无效。");
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::string Utf8(const std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring QuoteArgument(const std::wstring_view text) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto ch : text) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += ch;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}

fs::path ExecutablePath() {
    std::wstring path(32768, 0);
    const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) throw std::runtime_error("无法读取程序路径。");
    path.resize(size);
    return path;
}

fs::path CreateStage(const fs::path& root) {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("无法创建更新任务。");
    wchar_t name[40]{};
    StringFromGUID2(guid, name, 40);
    const auto stage = root / L".pal4plus-update" / name;
    CheckStage(root, stage);
    fs::create_directories(stage);
    return stage;
}

bool IsGameRunning(const fs::path& root) {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snapshot.value == INVALID_HANDLE_VALUE) throw std::runtime_error("无法检查游戏进程。");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.value, &entry)) return false;
    do {
        if (_wcsicmp(entry.szExeFile, L"PAL4.exe") && _wcsicmp(entry.szExeFile, L"launch.exe")) continue;
        Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)};
        if (!process.value) return true; // Do not assume an inaccessible game is safe to overwrite.
        std::wstring path(32768, 0);
        DWORD size = static_cast<DWORD>(path.size());
        if (!QueryFullProcessImageNameW(process.value, 0, path.data(), &size)) return true;
        path.resize(size);
        if (_wcsicmp(fs::path(path).parent_path().c_str(), root.c_str()) == 0) return true;
    } while (Process32NextW(snapshot.value, &entry));
    return false;
}

void ExtractPackage(const fs::path& stage, const Manifest& manifest, const std::function<bool()>& cancelled) {
    const auto root = RootFromStage(stage);
    CheckPath(root, stage / L"payload");
    if (fs::exists(stage / L"payload")) throw std::runtime_error("更新解压目录必须为空的新目录。");
    VerifyFile(stage / L"package.zip", manifest.package_size, manifest.package_sha256);
    Write(stage / L"manifest.json", FormatManifest(manifest));
    Write(stage / L"extract.ps1", kExtractScript);
    wchar_t system[32768]{};
    GetSystemDirectoryW(system, 32768);
    const auto powershell = fs::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
    auto child = Start(powershell, {L"-NoProfile", L"-NonInteractive", L"-ExecutionPolicy", L"Bypass", L"-File",
        (stage / L"extract.ps1").wstring(), L"-Package", (stage / L"package.zip").wstring(),
        L"-Destination", (stage / L"payload").wstring(), L"-Manifest", (stage / L"manifest.json").wstring()}, root, true);
    Handle process{child.hProcess}, thread{child.hThread};
    const auto start = GetTickCount64();
    while (WaitForSingleObject(process.value, 100) == WAIT_TIMEOUT) {
        if (cancelled() || GetTickCount64() - start > 120000) {
            TerminateProcess(process.value, 1);
            WaitForSingleObject(process.value, 5000);
            throw std::runtime_error("更新解压已取消或超时。");
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(process.value, &code);
    if (code != 0) throw std::runtime_error("更新包解压失败或内容不符合清单。");
    VerifyPayload(stage, manifest);
}

void VerifyPayload(const fs::path& stage, const Manifest& manifest) {
    const auto root = RootFromStage(stage);
    std::set<fs::path> expected;
    for (const auto& file : manifest.files) {
        const auto path = stage / L"payload" / fs::path(Wide(file.path));
        CheckPath(root, path);
        VerifyFile(path, file.size, file.sha256);
        expected.insert(path);
    }
    for (const auto& entry : fs::recursive_directory_iterator(stage / L"payload")) {
        CheckPath(root, entry.path());
        if (!entry.is_directory() && !expected.contains(entry.path())) throw std::runtime_error("更新包包含额外文件。");
    }
}

void InstallTransaction(const fs::path& root, const fs::path& stage, const Manifest& manifest) {
    CheckStage(root, stage);
    VerifyPayload(stage, manifest);
    if (IsGameRunning(root)) throw std::runtime_error("游戏仍在运行，请退出游戏后重试。");
    if (fs::exists(root / L".pal4plus-update/pending.json")) throw std::runtime_error("另一项更新尚未结束。");
    std::vector<std::string> paths = manifest.remove;
    for (const auto& file : manifest.files) paths.push_back(file.path);
    Json entries = Json::array();
    for (const auto& relative : paths) {
        if (!IsManagedPath(relative)) throw std::runtime_error("非托管文件拒绝更新。");
        const auto path = root / fs::path(Wide(relative));
        CheckPath(root, path);
        CheckPath(root, fs::path(path.wstring() + L".pal4plus-new"));
        const bool existed = fs::exists(path);
        if (existed) {
            const auto backup = stage / L"backup" / fs::path(Wide(relative));
            CheckPath(root, backup);
            fs::create_directories(backup.parent_path());
            fs::copy_file(path, backup, fs::copy_options::none);
        }
        entries.push_back({{"path", relative}, {"existed", existed}});
    }
    Write(stage / L"journal.json", Json{{"phase", "installing"}, {"files", entries}}.dump(2));
    Write(root / L".pal4plus-update/pending.json", Json{{"stage", Utf8(stage.wstring())}, {"pid", GetCurrentProcessId()}}.dump());
    try {
        // Update the launcher last. Each individual replacement is atomic;
        // the journal makes a partial multi-file update recoverable.
        auto files = manifest.files;
        std::stable_sort(files.begin(), files.end(), [](const File& a, const File& b) {
            return (a.path == "PAL4Plus.exe") < (b.path == "PAL4Plus.exe");
        });
        for (const auto& file : files) Replace(stage / L"payload" / fs::path(Wide(file.path)), root / fs::path(Wide(file.path)));
        for (const auto& path : manifest.remove) fs::remove(root / fs::path(Wide(path)));
        auto journal = ReadJournal(root, stage);
        journal["phase"] = "installed";
        Write(stage / L"journal.json", journal.dump(2));
    } catch (...) {
        RollbackTransaction(root, stage);
        throw;
    }
}

void RollbackTransaction(const fs::path& root, const fs::path& stage) {
    auto journal = ReadJournal(root, stage);
    if (journal.at("phase") == "committed") throw std::runtime_error("已完成的更新不能再次回滚。");
    for (const auto& file : journal.at("files")) {
        const auto path = fs::path(Wide(file.at("path").get<std::string>()));
        if (file.at("existed").get<bool>()) {
            // An untouched, locked file needs no replacement during rollback.
            if (!fs::exists(root / path) || Sha256(root / path) != Sha256(stage / L"backup" / path))
                Replace(stage / L"backup" / path, root / path);
        } else {
            fs::remove(root / path);
        }
    }
    journal["phase"] = "rolled_back";
    Write(stage / L"journal.json", journal.dump(2));
    fs::remove(root / L".pal4plus-update/pending.json");
}

void CommitTransaction(const fs::path& root, const fs::path& stage) {
    auto journal = ReadJournal(root, stage);
    journal["phase"] = "committed";
    Write(stage / L"journal.json", journal.dump(2));
    // Once committed, cleanup failure must never trigger rollback.
    std::error_code ignored;
    fs::remove(root / L".pal4plus-update/pending.json", ignored);
    // Keep the small journal/helper until the next normal launcher startup.
    fs::remove_all(stage / L"backup", ignored);
    fs::remove_all(stage / L"payload", ignored);
    fs::remove(stage / L"package.zip", ignored);
}

bool RecoverInterruptedUpdate(const fs::path& root) {
    const auto pending = root / L".pal4plus-update/pending.json";
    CheckPath(root, pending);
    if (fs::exists(pending)) {
        Handle lock{LockInstallation(root)};
        const auto json = Json::parse(Read(pending));
        const auto stage = fs::path(Wide(json.at("stage").get<std::string>()));
        const auto journal = ReadJournal(root, stage);
        if (journal.at("phase") == "committed" || journal.at("phase") == "rolled_back") fs::remove(pending);
        else {
            // Recovery also runs in a copied helper so a partially updated
            // launcher does not try to replace its own mapped executable.
            LaunchInstaller(root, stage);
            return true;
        }
    }
    const auto tasks = root / L".pal4plus-update";
    if (!fs::exists(tasks)) return false;
    for (const auto& entry : fs::directory_iterator(tasks)) {
        if (!entry.is_directory()) continue;
        CheckPath(root, entry.path());
        const auto journal = entry.path() / L"journal.json";
        if (!fs::exists(journal)) continue;
        const auto phase = Json::parse(Read(journal)).value("phase", std::string{});
        if (phase == "committed" || phase == "rolled_back") {
            std::error_code ignored;
            fs::remove_all(entry.path(), ignored);
        }
    }
    return false;
}

void LaunchInstaller(const fs::path& root, const fs::path& stage) {
    CheckStage(root, stage);
    const auto helper = stage / L"updater.exe";
    fs::copy_file(ExecutablePath(), helper, fs::copy_options::overwrite_existing);
    Write(stage / L"job.json", Json{{"parent_pid", GetCurrentProcessId()}}.dump());
    auto process = Start(helper, {L"--apply-update", (stage / L"job.json").wstring()}, root, true);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

int RunInstaller(const fs::path& job_path, const std::function<void(const std::wstring&)>& report_error) {
    fs::path root, stage;
    bool installed = false;
    bool new_ready = false;
    Handle installation_lock;
    try {
        stage = job_path.parent_path();
        root = RootFromStage(stage);
        const auto job = Json::parse(Read(job_path));
        Handle parent{OpenProcess(SYNCHRONIZE, FALSE, job.at("parent_pid").get<DWORD>())};
        if (parent.value && WaitForSingleObject(parent.value, 30000) != WAIT_OBJECT_0)
            throw std::runtime_error("启动器未退出，更新已取消。");
        installation_lock.value = LockInstallation(root);
        if (fs::exists(stage / L"journal.json")) {
            const auto phase = ReadJournal(root, stage).at("phase").get<std::string>();
            if (phase != "committed") RollbackTransaction(root, stage);
            auto old = Start(root / L"PAL4Plus.exe", {}, root);
            CloseHandle(old.hThread); CloseHandle(old.hProcess);
            return 0;
        }
        const auto manifest = ParseManifest(Read(stage / L"manifest.json"));
        InstallTransaction(root, stage, manifest);
        installed = true;
        const auto event_name = L"Local\\PAL4PlusUpdateReady-" + stage.filename().wstring();
        Handle ready{CreateEventW(nullptr, TRUE, FALSE, event_name.c_str())};
        if (!ready.value) throw std::runtime_error("无法等待新版启动器就绪。");
        auto child = Start(root / L"PAL4Plus.exe", {L"--update-ready", event_name}, root);
        Handle process{child.hProcess}, thread{child.hThread};
        HANDLE waits[]{ready.value, process.value};
        if (WaitForMultipleObjects(2, waits, FALSE, 90000) != WAIT_OBJECT_0) {
            // Only the new launcher created by this update may be stopped.
            if (WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT) {
                TerminateProcess(process.value, 1);
                WaitForSingleObject(process.value, 5000);
            }
            throw std::runtime_error("新版启动器未能正常启动，已恢复旧版本。");
        }
        new_ready = true;
        CommitTransaction(root, stage);
        return 0;
    } catch (const std::exception& error) {
        auto message = Wide(error.what());
        if (!new_ready && installation_lock.value && installation_lock.value != INVALID_HANDLE_VALUE) {
            try {
                if (installed) RollbackTransaction(root, stage);
                // InstallTransaction already rolls back replacement failures.
                // Leave an unfinished journal for recovery if that rollback failed.
                if (fs::exists(root / L".pal4plus-update/pending.json")) throw std::runtime_error("更新恢复日志仍未完成。");
                auto old = Start(root / L"PAL4Plus.exe", {}, root);
                CloseHandle(old.hThread); CloseHandle(old.hProcess);
            } catch (const std::exception& recovery_error) {
                message += L"\n恢复尚未完成，请保留更新备份：\n" + stage.wstring() + L"\n" + Wide(recovery_error.what());
            }
        }
        if (report_error) report_error(message);
        else MessageBoxW(nullptr, message.c_str(), L"PAL4Plus 更新", MB_OK | MB_ICONERROR);
        return 1;
    }
}

void SetReadyEvent(std::wstring name) { g_ready_event = std::move(name); }
void SignalReady() {
    if (g_ready_event.empty()) return;
    Handle event{OpenEventW(EVENT_MODIFY_STATE, FALSE, g_ready_event.c_str())};
    if (event.value) SetEvent(event.value);
    g_ready_event.clear();
}
}  // namespace pal4::inject::update

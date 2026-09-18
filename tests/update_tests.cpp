#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <iostream>
#include <functional>
#include <crtdbg.h>
#include "update_installer.h"
#include "update_client.h"
#include <map>
#include <atomic>
#include <thread>
#include <shellapi.h>
#include "nlohmann/json.hpp"
using namespace pal4::inject::update;
namespace fs = std::filesystem;
using Json = nlohmann::json;
fs::path child_log;
void Write(const fs::path& p, const std::string& s) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << s; }
std::string Read(const fs::path& p) { std::ifstream in(p, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }
void Reject(const std::function<void()>& fn) { bool rejected=false; try { fn(); } catch(const std::exception&) { rejected=true; } assert(rejected); }
PROCESS_INFORMATION Spawn(const fs::path& exe, const std::vector<std::wstring>& args, const fs::path& cwd) {
    auto cmd=QuoteArgument(exe.wstring()); for(const auto& arg:args) cmd+=L" "+QuoteArgument(arg);
    STARTUPINFOW si{}; si.cb=sizeof(si); PROCESS_INFORMATION pi{};
    child_log = cwd / "test-child.log";
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    auto log = CreateFileW(child_log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_ALWAYS, 0, nullptr);
    assert(log != INVALID_HANDLE_VALUE);
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = si.hStdError = log;
    assert(CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,cwd.c_str(),&si,&pi));
    CloseHandle(log); return pi;
}
DWORD Finish(PROCESS_INFORMATION pi) {
    assert(WaitForSingleObject(pi.hProcess,30000)==WAIT_OBJECT_0); DWORD code=0;
    assert(GetExitCodeProcess(pi.hProcess,&code)); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    if (code) std::cerr << Read(child_log); return code;
}
struct Fixture {
    fs::path root,stage; Manifest manifest;
    Fixture() {
        root=CreateStage(fs::temp_directory_path())/L"中文 game space"; fs::create_directories(root); stage=CreateStage(root);
        manifest.version="9.0.0"; manifest.package_name="PAL4Plus_v9.0.0_update_win32.zip";
        manifest.package_size=1; manifest.package_sha256=std::string(64,'a');
        for(const std::string p:{"PAL4Plus.exe","pal4_inject/runtime.dll","pal4_inject/cli.exe"}) {
            Write(root/p,"old "+p); Write(stage/"payload"/p,"new "+p);
        }
        Write(root/"save/slot.sav","my save"); Write(root/"config.cfg","my display settings");
        Write(root/"pal4_inject/inject_settings.ini","my controller settings"); Refresh();
    }
    void Refresh() {
        manifest.files.clear();
        for(const std::string p:{"PAL4Plus.exe","pal4_inject/runtime.dll","pal4_inject/cli.exe"}) {
            const auto payload=stage/"payload"/p; manifest.files.push_back({p,fs::file_size(payload),Sha256(payload)});
        }
        Write(stage/"manifest.json",FormatManifest(manifest));
    }
    void UserData() {
        assert(Read(root/"save/slot.sav")=="my save"); assert(Read(root/"config.cfg")=="my display settings");
        assert(Read(root/"pal4_inject/inject_settings.ini")=="my controller settings");
    }
    ~Fixture() { std::error_code ignored; fs::remove_all(root.parent_path(),ignored); }
};
void Parsing() {
    assert(IsNewerVersion("v0.2.10","v0.2.9")); assert(IsNewerVersion("1.0.0","0.99.99"));
    assert(!IsNewerVersion("v0.2.2","0.2.2")); assert(!IsNewerVersion("0.2.1","0.2.2"));
    Version v{};
    for(const auto* s:{"","1.2","1.2.3.4","v01.2.3","1.2.-1","1.2.3-beta","1.2.3+build","4294967296.0.0"," 1.2.3"}) assert(!ParseVersion(s,&v));
    assert(!IsNewerVersion("1.0.0","invalid")); Fixture f;
    const auto original=FormatManifest(f.manifest); assert(ParseManifest(original).files.size()==3);
    const auto invalid=[&](const auto& mutate) { auto j=Json::parse(original); mutate(j); Reject([&]{ParseManifest(j.dump());}); };
    invalid([](auto& j){j["platform"]="win64";}); invalid([](auto& j){j["schema_version"]=2;});
    invalid([](auto& j){j["channel"]="beta";}); invalid([](auto& j){j["package"]["size"]=-1;});
    invalid([](auto& j){j["package"]["size"]=300ULL*1024*1024;}); invalid([](auto& j){j["package"]["sha256"]="bad";});
    invalid([](auto& j){j["package"]["name"]="PAL4Plus_v8.0.0_update_win32.zip";});
    invalid([](auto& j){j["files"].push_back(j["files"][0]);}); invalid([](auto& j){j["files"].erase(0);});
    for(const auto* p:{"../PAL4Plus.exe","C:/PAL4Plus.exe","PAL4.exe","save/a.sav","config.cfg","pal4_inject/inject_settings.ini","PAL4_inject/runtime.dll"}) {
        invalid([&](auto& j){j["files"][0]["path"]=p;}); invalid([&](auto& j){j["remove"]=Json::array({p});});
    }
    invalid([](auto& j){j["remove"]=Json::array({"PAL4Plus.exe"});});
    auto gh=Json{{"tag_name","v9.0.0"},{"body","notes"},{"assets",Json::array({{{"name","update.json"},{"browser_download_url","https://example.com/update.json"}}})}};
    assert(ParseRelease(gh.dump()).manifest_url=="https://example.com/update.json");
    auto ge=gh; ge["assets"]={{"links",Json::array({{{"name","update.json"},{"url","https://gitee.com/file"}}})}};
    // Gitee's nested author URL must never be mistaken for a release asset.
    ge["author"]={{"html_url","https://gitee.com/betesla"}};
    assert(ParseRelease(ge.dump()).manifest_url=="https://gitee.com/file");
    gh["prerelease"]=true; Reject([&]{ParseRelease(gh.dump());}); gh["prerelease"]=false;
    gh["draft"]=true; Reject([&]{ParseRelease(gh.dump());}); gh["draft"]=false;
    gh["assets"][0]["browser_download_url"]="http://example.com/update.json"; assert(ParseRelease(gh.dump()).manifest_url.empty());
    assert(!IsHttpsUrl("https://user@host/file")); Write(f.root/"hash","abc");
    assert(Sha256(f.root/"hash")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Reject([&]{VerifyFile(f.root/"hash",3,std::string(64,'0'));});
    for(const std::wstring arg:{L"",L"中文 space",L"a\\\"b",L"C:\\space dir\\"}) {
        int argc=0; auto argv=CommandLineToArgvW((L"test.exe "+QuoteArgument(arg)).c_str(),&argc);
        assert(argv && argc==2 && argv[1]==arg); LocalFree(argv);
    }
    std::cout<<"Parsing, versions, SHA256 and quoting passed\n";
}
void Transactions() {
    {
        Fixture f; fs::remove(f.root/"pal4_inject/cli.exe"); Write(f.root/"PAL4_inject.exe","legacy");
        f.manifest.remove={"PAL4_inject.exe"}; InstallTransaction(f.root,f.stage,f.manifest);
        assert(Read(f.root/"pal4_inject/runtime.dll")=="new pal4_inject/runtime.dll"); assert(!fs::exists(f.root/"PAL4_inject.exe"));
        RollbackTransaction(f.root,f.stage); assert(Read(f.root/"PAL4Plus.exe")=="old PAL4Plus.exe");
        assert(Read(f.root/"PAL4_inject.exe")=="legacy"); assert(!fs::exists(f.root/"pal4_inject/cli.exe"));
        RollbackTransaction(f.root,f.stage); f.UserData();
    }
    {
        Fixture f; HANDLE locked=CreateFileW((f.root/"PAL4Plus.exe").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        assert(locked!=INVALID_HANDLE_VALUE); Reject([&]{InstallTransaction(f.root,f.stage,f.manifest);}); CloseHandle(locked);
        assert(Read(f.root/"pal4_inject/runtime.dll")=="old pal4_inject/runtime.dll"); assert(Read(f.root/"PAL4Plus.exe")=="old PAL4Plus.exe");
        assert(!fs::exists(f.root/".pal4plus-update/pending.json")); f.UserData();
    }
    {
        Fixture f; Write(f.stage/"payload/pal4_inject/runtime.dll","corrupt"); Reject([&]{InstallTransaction(f.root,f.stage,f.manifest);});
        assert(Read(f.root/"PAL4Plus.exe")=="old PAL4Plus.exe");
    }
    {
        Fixture f; InstallTransaction(f.root,f.stage,f.manifest);
        const auto backup = f.stage / "backup/PAL4Plus.exe";
        const auto locked = CreateFileW(backup.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        assert(locked != INVALID_HANDLE_VALUE);
        CommitTransaction(f.root,f.stage); // cleanup failure must not undo a committed update
        assert(Read(f.root / "PAL4Plus.exe") == "new PAL4Plus.exe");
        CloseHandle(locked);
        Reject([&]{RollbackTransaction(f.root,f.stage);});
        assert(!RecoverInterruptedUpdate(f.root)); assert(!fs::exists(f.stage)); f.UserData();
    }
    std::cout<<"Transactions, locked-file rollback and settings preservation passed\n";
}
void DownloadLifecycle(const fs::path& root, const fs::path& zip, const Manifest& manifest);
void Packages() {
    Fixture f; const auto output=f.root/"packages"; const auto script=f.root/"package-test.ps1";
    Write(script,"param($Source,$Output,$Module)\n$ErrorActionPreference='Stop'\n. $Module\nNew-UpdatePackage -Source $Source -Output $Output -Version v9.0.0 -Notes 'fixture'\n");
    wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system,MAX_PATH); const auto ps=fs::path(system)/"WindowsPowerShell/v1.0/powershell.exe";
    assert(Finish(Spawn(ps,{L"-NoProfile",L"-ExecutionPolicy",L"Bypass",L"-File",script.wstring(),(f.stage/"payload").wstring(),output.wstring(),
        (fs::path(PAL4_SOURCE_DIR)/"scripts/update-package.ps1").wstring()},f.root))==0);
    auto manifest=ParseManifest(Read(output/"update.json")); const auto stage=CreateStage(f.root);
    DownloadLifecycle(f.root, output / manifest.package_name, manifest);
    fs::copy_file(output/manifest.package_name,stage/"package.zip"); ExtractPackage(stage,manifest,[]{return false;});
    assert(Read(stage/"payload/PAL4Plus.exe")=="new PAL4Plus.exe"); Reject([&]{ExtractPackage(stage,manifest,[]{return false;});});
    Write(script,"param($Zip,$Entry)\n$ErrorActionPreference='Stop'\nAdd-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem\n$a=[IO.Compression.ZipFile]::Open($Zip,[IO.Compression.ZipArchiveMode]::Update)\ntry { $a.CreateEntry($Entry) | Out-Null } finally { $a.Dispose() }\n");
    for(const auto* entry:{L"../escaped.txt",L"PAL4Plus.exe",L"unlisted.txt"}) {
        const auto bad=CreateStage(f.root); fs::copy_file(output/manifest.package_name,bad/"package.zip");
        assert(Finish(Spawn(ps,{L"-NoProfile",L"-ExecutionPolicy",L"Bypass",L"-File",script.wstring(),(bad/"package.zip").wstring(),entry},f.root))==0);
        auto modified=manifest; modified.package_size=fs::file_size(bad/"package.zip"); modified.package_sha256=Sha256(bad/"package.zip");
        Reject([&]{ExtractPackage(bad,modified,[]{return false;});}); assert(!fs::exists(bad/"escaped.txt"));
    }
    std::cout<<"Release ZIP, extraction, traversal/duplicate/unlisted rejection passed\n";
}
void Helpers() {
    const auto exe=ExecutablePath(); const auto fixture=exe.parent_path()/"pal4_update_fixture.exe";
    for(const bool fail:{false,true}) {
        Fixture f; fs::copy_file(fixture,f.root/"PAL4Plus.exe",fs::copy_options::overwrite_existing);
        fs::copy_file(fixture,f.stage/"payload/PAL4Plus.exe",fs::copy_options::overwrite_existing); f.Refresh();
        if(fail) Write(f.root/"fail-start","yes"); Write(f.stage/"job.json",Json{{"parent_pid",0}}.dump());
        assert(Finish(Spawn(exe,{L"--apply-update",(f.stage/"job.json").wstring()},f.root))==(fail?1UL:0UL));
        assert(Read(f.root/"pal4_inject/runtime.dll")==(fail?"old pal4_inject/runtime.dll":"new pal4_inject/runtime.dll"));
        assert(!fs::exists(f.root/".pal4plus-update/pending.json")); f.UserData();
    }
    {
        Fixture f; fs::copy_file(fixture,f.root/"PAL4Plus.exe",fs::copy_options::overwrite_existing);
        InstallTransaction(f.root,f.stage,f.manifest); Write(f.stage/"job.json",Json{{"parent_pid",0}}.dump());
        assert(Finish(Spawn(exe,{L"--apply-update",(f.stage/"job.json").wstring()},f.root))==0);
        assert(Sha256(f.root/"PAL4Plus.exe")==Sha256(fixture)); assert(Read(f.root/"pal4_inject/runtime.dll")=="old pal4_inject/runtime.dll"); f.UserData();
    }
    {
        Fixture f; fs::copy_file(fixture,f.root/"PAL4.exe"); auto game=Spawn(f.root/"PAL4.exe",{L"--wait"},f.root);
        assert(IsGameRunning(f.root)); Reject([&]{InstallTransaction(f.root,f.stage,f.manifest);});
        assert(WaitForSingleObject(game.hProcess,0)==WAIT_TIMEOUT); TerminateProcess(game.hProcess,0); Finish(game);
        assert(!IsGameRunning(f.root));
    }
    std::cout<<"Helper readiness, failed-start rollback, interrupted recovery and game protection passed\n";
}
struct FakeTransport : Transport {
    std::map<std::string, std::string> pages;
    std::atomic_bool cancelled = false;
    std::atomic_int downloads = 0;
    void Cancel() override { cancelled = true; }
    void Reset() override { cancelled = false; }
    bool Cancelled() const override { return cancelled; }
    std::string Get(const std::string& url) override {
        if (!pages.contains(url)) throw std::runtime_error("offline");
        return pages.at(url);
    }
    void Download(const std::string&, const fs::path&, std::uint64_t, const std::function<void(float)>&) override {
        ++downloads;
        throw std::runtime_error("simulated disconnect");
    }
};
struct FileTransport final : FakeTransport {
    fs::path zip;
    explicit FileTransport(fs::path path) : zip(std::move(path)) {}
    void Download(const std::string&, const fs::path& target, std::uint64_t, const std::function<void(float)>& progress) override {
        fs::copy_file(zip, target, fs::copy_options::overwrite_existing);
        progress(1.0F);
    }
};
void DownloadLifecycle(const fs::path& root, const fs::path& zip, const Manifest& manifest) {
    auto transport = std::make_unique<FileTransport>(zip);
    transport->pages["https://gitee.com/api/v5/repos/betesla/pal4_inject/releases/latest"] =
        Json{{"tag_name", "v9.0.0"}, {"assets", Json::array({
            {{"name", "update.json"}, {"browser_download_url", "https://fixture.test/update.json"}},
            {{"name", manifest.package_name}, {"browser_download_url", "https://fixture.test/package.zip"}}
        })}}.dump();
    transport->pages["https://fixture.test/update.json"] = FormatManifest(manifest);
    const auto wait = [](Client& client, Phase phase) {
        const auto start = GetTickCount64();
        while (client.Snapshot().phase != phase) {
            assert(client.Snapshot().phase != Phase::failed);
            assert(GetTickCount64() - start < 10000);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };
    fs::copy_file(ExecutablePath().parent_path() / "pal4_update_fixture.exe", root / "PAL4.exe");
    auto game = Spawn(root / "PAL4.exe", {L"--wait"}, root);
    fs::path prepared;
    {
        Client client(root, "0.2.2", std::move(transport));
        wait(client, Phase::available);
        client.Download(); wait(client, Phase::waiting_for_game);
        client.Cancel();
        assert(client.Snapshot().phase == Phase::available);
        assert(WaitForSingleObject(game.hProcess, 0) == WAIT_TIMEOUT);
        client.Download(); wait(client, Phase::waiting_for_game);
        TerminateProcess(game.hProcess, 0); Finish(game);
        wait(client, Phase::ready);
        prepared = client.TakeReadyStage();
        assert(!prepared.empty());
    }
    VerifyPayload(prepared, manifest); // ownership transferred; client destruction keeps it
    fs::remove_all(prepared);
    std::cout << "Download, cancel, game-exit wait and ready-stage transfer passed\n";
}
Status WaitClient(Client& client, bool download = false) {
    const auto start = GetTickCount64();
    for (;;) {
        const auto status = client.Snapshot();
        if ((!download && status.phase != Phase::checking) || (download && status.phase == Phase::failed)) return status;
        assert(GetTickCount64() - start < 3000);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
void NetworkPolicy() {
    const std::string gitee = "https://gitee.com/api/v5/repos/betesla/pal4_inject/releases/latest";
    const std::string github = "https://api.github.com/repos/betesla/pal4_inject/releases/latest";
    Fixture f;
    const auto release = [&](const std::string& version, const std::string& origin) {
        return Json{{"tag_name", version}, {"assets", Json::array({
            {{"name", "update.json"}, {"browser_download_url", origin + "/update.json"}},
            {{"name", f.manifest.package_name}, {"browser_download_url", origin + "/package.zip"}}
        })}}.dump();
    };
    {
        auto transport = std::make_unique<FakeTransport>();
        Client client(f.root, "0.2.2", std::move(transport));
        assert(WaitClient(client).phase == Phase::idle);
    }
    {
        auto transport = std::make_unique<FakeTransport>();
        transport->pages[github] = release("v9.0.0", "https://github.test");
        transport->pages["https://github.test/update.json"] = FormatManifest(f.manifest);
        Client client(f.root, "0.2.2", std::move(transport));
        assert(WaitClient(client).version == "9.0.0");
    }
    {
        auto transport = std::make_unique<FakeTransport>();
        transport->pages[gitee] = release("v10.0.0", "https://gitee.test");
        transport->pages["https://gitee.test/update.json"] = FormatManifest(f.manifest);
        Client client(f.root, "0.2.2", std::move(transport));
        assert(WaitClient(client).phase == Phase::idle);
    }
    {
        auto transport = std::make_unique<FakeTransport>();
        transport->pages[gitee] = release("v9.0.0", "https://gitee.test");
        transport->pages["https://gitee.test/update.json"] = FormatManifest(f.manifest);
        Client client(f.root, "10.0.0", std::move(transport));
        assert(WaitClient(client).phase == Phase::idle);
    }
    {
        auto transport = std::make_unique<FakeTransport>();
        auto* mock = transport.get();
        transport->pages[gitee] = release("v9.0.0", "https://gitee.test");
        transport->pages[github] = release("v9.0.0", "https://github.test");
        transport->pages["https://gitee.test/update.json"] = FormatManifest(f.manifest);
        transport->pages["https://github.test/update.json"] = FormatManifest(f.manifest);
        Client client(f.root, "0.2.2", std::move(transport));
        assert(WaitClient(client).phase == Phase::available);
        client.Download();
        assert(WaitClient(client, true).message == "simulated disconnect");
        assert(mock->downloads == 2);
        client.Download();
        assert(WaitClient(client, true).phase == Phase::failed);
        assert(mock->downloads == 4);
        f.UserData();
    }
    std::cout << "Offline network, tag mismatch, no downgrade, mirror fallback and retry passed\n";
}
int wmain(int argc,wchar_t** argv) {
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    std::cout << std::unitbuf;
    if(argc==3 && std::wstring_view(argv[1])==L"--apply-update") return RunInstaller(argv[2],[](const auto&){});
    try { Parsing(); Transactions(); Packages(); Helpers(); NetworkPolicy(); std::cout<<"All updater checks passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

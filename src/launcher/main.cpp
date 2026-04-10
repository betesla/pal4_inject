#include <filesystem>
#include <iostream>
#include <string>

#include "pal4inject/launcher.h"

namespace {

std::filesystem::path CurrentExecutableDirectory() {
    char buffer[MAX_PATH];
    const DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return len == 0
        ? std::filesystem::current_path()
        : std::filesystem::path(std::string(buffer, len)).parent_path();
}

void PrintUsage() {
    std::cout
        << "Usage: pal4_injector_launcher (--game-root <path> | --exe <path>) "
        << "[--dll <path>] [--script-mode cs|csb] [--ready-timeout-ms <ms>] [--no-resume]\n";
}

}  // namespace

int main(int argc, char** argv) {
    pal4::inject::LaunchOptions options;
    options.dll_path = CurrentExecutableDirectory() / "pal4_runtime_x86.dll";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--game-root" && i + 1 < argc) {
            options.game_root = argv[++i];
        } else if (arg == "--exe" && i + 1 < argc) {
            options.executable_path = argv[++i];
        } else if (arg == "--dll" && i + 1 < argc) {
            options.dll_path = argv[++i];
        } else if (arg == "--script-mode" && i + 1 < argc) {
            if (!pal4::inject::TryParseScriptMode(argv[++i], &options.script_mode) ||
                options.script_mode == pal4::inject::ScriptMode::inherit) {
                std::cerr << "invalid --script-mode, expected cs or csb\n";
                return 1;
            }
        } else if (arg == "--ready-timeout-ms" && i + 1 < argc) {
            options.ready_timeout_ms = static_cast<DWORD>(std::stoul(argv[++i]));
        } else if (arg == "--no-resume") {
            options.resume_after_ready = false;
        } else if (arg == "--arg" && i + 1 < argc) {
            options.child_args.push_back(argv[++i]);
        } else {
            PrintUsage();
            return 1;
        }
    }

    if (options.game_root.empty() && options.executable_path.empty()) {
        PrintUsage();
        return 1;
    }

    pal4::inject::InjectedProcess process{};
    const auto result = pal4::inject::LaunchInjectedProcess(options, &process);
    if (!result.ok) {
        std::cerr << result.error << "\n";
        return 1;
    }

    std::cout
        << "ok pid=" << result.process_id
        << " pipe=" << result.pipe_name
        << " ready_event=" << result.ready_event_name
        << " script_mode=" << pal4::inject::ToString(result.script_mode)
        << " resumed=" << (options.resume_after_ready ? 1 : 0)
        << "\n";

    process.Close();
    return 0;
}

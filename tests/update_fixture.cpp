#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string_view>

// A real PE executable used only inside temporary test installations.
int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--wait") { Sleep(60000); return 0; }
    if (argc == 3 && std::wstring_view(argv[1]) == L"--update-ready") {
        if (std::filesystem::exists("fail-start")) return 2;
        const auto event = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[2]);
        if (!event) return 3;
        SetEvent(event);
        CloseHandle(event);
        return 0;
    }
    std::ofstream("old-launcher-reopened") << "ok";
    return 0;
}

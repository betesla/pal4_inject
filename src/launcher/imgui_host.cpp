#include "imgui_host.h"

#include <d3d9.h>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam);

namespace pal4::inject::launcher {
namespace {

IDirect3D9* g_d3d = nullptr;
IDirect3DDevice9* g_device = nullptr;
bool g_device_lost = false;
UINT g_resize_width = 0;
UINT g_resize_height = 0;
D3DPRESENT_PARAMETERS g_present_parameters{};

void CleanupDevice() {
    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }
    if (g_d3d) {
        g_d3d->Release();
        g_d3d = nullptr;
    }
}

bool CreateDevice(const HWND hwnd) {
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) {
        return false;
    }
    g_present_parameters = {};
    g_present_parameters.Windowed = TRUE;
    g_present_parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_present_parameters.BackBufferFormat = D3DFMT_UNKNOWN;
    g_present_parameters.EnableAutoDepthStencil = TRUE;
    g_present_parameters.AutoDepthStencilFormat = D3DFMT_D16;
    g_present_parameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    HRESULT result = g_d3d->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &g_present_parameters,
        &g_device);
    if (FAILED(result)) {
        result = g_d3d->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &g_present_parameters,
            &g_device);
    }
    return SUCCEEDED(result);
}

bool ResetDevice() {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    const HRESULT result = g_device->Reset(&g_present_parameters);
    if (FAILED(result)) {
        return false;
    }
    ImGui_ImplDX9_CreateDeviceObjects();
    return true;
}

LRESULT WINAPI HostWindowProc(
    const HWND hwnd,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam)) {
        return TRUE;
    }
    switch (message) {
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            g_resize_width = LOWORD(lparam);
            g_resize_height = HIWORD(lparam);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0U) == SC_KEYMENU) {
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void ConfigureStyle(const float dpi_scale) {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 10.0F;
    style.ChildRounding = 8.0F;
    style.FrameRounding = 6.0F;
    style.PopupRounding = 6.0F;
    style.ScrollbarRounding = 8.0F;
    style.WindowPadding = {18.0F, 16.0F};
    style.FramePadding = {10.0F, 7.0F};
    style.ItemSpacing = {10.0F, 9.0F};
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055F, 0.067F, 0.090F, 1.0F);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.075F, 0.090F, 0.120F, 1.0F);
    style.Colors[ImGuiCol_Header] = ImVec4(0.12F, 0.28F, 0.36F, 1.0F);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.16F, 0.38F, 0.48F, 1.0F);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.18F, 0.45F, 0.56F, 1.0F);
    style.Colors[ImGuiCol_Button] = ImVec4(0.10F, 0.36F, 0.45F, 1.0F);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.13F, 0.47F, 0.58F, 1.0F);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.10F, 0.31F, 0.39F, 1.0F);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.30F, 0.88F, 0.86F, 1.0F);
    style.ScaleAllSizes(dpi_scale);
    style.FontScaleDpi = dpi_scale;
}

void LoadLauncherFont(ImGuiIO& io) {
    constexpr const char* kChineseFontCandidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\simsun.ttc",
    };
    for (const char* const path : kChineseFontCandidates) {
        const DWORD attributes = GetFileAttributesA(path);
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        if (io.Fonts->AddFontFromFileTTF(
                path,
                18.0F,
                nullptr,
                io.Fonts->GetGlyphRangesChineseFull())) {
            return;
        }
    }
    io.Fonts->AddFontDefault();
}

}  // namespace

bool RunImGuiHost(
    const wchar_t* const title,
    const int client_width,
    const int client_height,
    const ImGuiFrameCallback frame_callback,
    void* const context,
    std::wstring* const error) {
    if (!frame_callback) {
        if (error) {
            *error = L"ImGui frame callback is null.";
        }
        return false;
    }

    ImGui_ImplWin32_EnableDpiAwareness();
    const float dpi_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    WNDCLASSEXW window_class{
        sizeof(WNDCLASSEXW),
        CS_CLASSDC,
        &HostWindowProc,
        0,
        0,
        GetModuleHandleW(nullptr),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        L"PAL4InjectImGuiLauncher",
        nullptr,
    };
    RegisterClassExW(&window_class);

    RECT window_rect{0, 0, client_width, client_height};
    AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);
    const int width = window_rect.right - window_rect.left;
    const int height = window_rect.bottom - window_rect.top;
    const HWND hwnd = CreateWindowW(
        window_class.lpszClassName,
        title,
        WS_OVERLAPPEDWINDOW,
        (GetSystemMetrics(SM_CXSCREEN) - width) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
        width,
        height,
        nullptr,
        nullptr,
        window_class.hInstance,
        nullptr);
    if (!hwnd || !CreateDevice(hwnd)) {
        CleanupDevice();
        if (hwnd) {
            DestroyWindow(hwnd);
        }
        UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
        if (error) {
            *error = L"无法初始化启动器的 DirectX 9 界面。";
        }
        return false;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ConfigureStyle(dpi_scale);
    LoadLauncherFont(io);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_device);

    bool done = false;
    while (!done) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) {
                done = true;
            }
        }
        if (done) {
            break;
        }

        if (g_device_lost) {
            const HRESULT cooperative_level = g_device->TestCooperativeLevel();
            if (cooperative_level == D3DERR_DEVICELOST) {
                Sleep(10);
                continue;
            }
            if (cooperative_level == D3DERR_DEVICENOTRESET && !ResetDevice()) {
                Sleep(10);
                continue;
            }
            g_device_lost = false;
        }
        if (g_resize_width != 0 && g_resize_height != 0) {
            g_present_parameters.BackBufferWidth = g_resize_width;
            g_present_parameters.BackBufferHeight = g_resize_height;
            g_resize_width = 0;
            g_resize_height = 0;
            if (!ResetDevice()) {
                g_device_lost = true;
                continue;
            }
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        if (!frame_callback(hwnd, context)) {
            done = true;
        }

        ImGui::EndFrame();
        g_device->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        g_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(9, 11, 15), 1.0F, 0);
        if (SUCCEEDED(g_device->BeginScene())) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_device->EndScene();
        }
        if (g_device->Present(nullptr, nullptr, nullptr, nullptr) == D3DERR_DEVICELOST) {
            g_device_lost = true;
        }
    }

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    if (IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
    UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
    if (error) {
        error->clear();
    }
    return true;
}

}  // namespace pal4::inject::launcher

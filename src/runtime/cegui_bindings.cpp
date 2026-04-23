#include "cegui_bindings.h"

#include <mutex>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace pal4::inject {
namespace {

std::string FormatWindowsError(const DWORD code) {
    char* buffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);
    std::string text;
    if (size && buffer) {
        text.assign(buffer, size);
        LocalFree(buffer);
    } else {
        text = "Windows error " + std::to_string(code);
    }
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

bool ResolveBinding(
    const HMODULE module,
    const char* symbol_name,
    FARPROC* out_proc,
    std::string* error) {
    *out_proc = GetProcAddress(module, symbol_name);
    if (*out_proc) {
        return true;
    }
    if (error) {
        *error = std::string("GetProcAddress failed for ") + symbol_name + ": " +
            FormatWindowsError(GetLastError());
    }
    return false;
}

void ResolveOptionalBinding(
    const HMODULE module,
    const char* symbol_name,
    FARPROC* out_proc) {
    if (!out_proc) {
        return;
    }
    *out_proc = GetProcAddress(module, symbol_name);
}

}  // namespace

bool TryGetCeguiBindings(CeguiBindings* out, std::string* error) {
    if (!out) {
        if (error) {
            *error = "output binding pointer is null";
        }
        return false;
    }

    static std::mutex mutex;
    static bool resolved = false;
    static CeguiBindings cached{};

    std::scoped_lock lock(mutex);
    if (resolved) {
        *out = cached;
        return true;
    }

    const HMODULE module = GetModuleHandleA("CEGUIBase.dll");
    if (!module) {
        if (error) {
            *error = "CEGUIBase.dll is not loaded";
        }
        return false;
    }

    FARPROC proc = nullptr;
    if (!ResolveBinding(module, "?getSingleton@System@CEGUI@@SAAAV12@XZ", &proc, error)) {
        return false;
    }
    cached.get_system_singleton = reinterpret_cast<CeguiBindings::GetSingletonFn>(proc);

    if (!ResolveBinding(module, "?getSingletonPtr@System@CEGUI@@SAPAV12@XZ", &proc, error)) {
        return false;
    }
    cached.get_system_singleton_ptr = reinterpret_cast<CeguiBindings::GetSingletonPtrFn>(proc);

    if (!ResolveBinding(module, "?getGUISheet@System@CEGUI@@QBEPAVWindow@2@XZ", &proc, error)) {
        return false;
    }
    cached.get_gui_sheet = reinterpret_cast<CeguiBindings::SystemGetGuiSheetFn>(proc);

    if (!ResolveBinding(module, "?getSingletonPtr@FontManager@CEGUI@@SAPAV12@XZ", &proc, error)) {
        return false;
    }
    cached.get_font_manager_singleton_ptr = reinterpret_cast<CeguiBindings::GetSingletonPtrFn>(proc);

    if (!ResolveBinding(module, "?getSingletonPtr@ImagesetManager@CEGUI@@SAPAV12@XZ", &proc, error)) {
        return false;
    }
    cached.get_imageset_manager_singleton_ptr =
        reinterpret_cast<CeguiBindings::GetSingletonPtrFn>(proc);

    if (!ResolveBinding(module, "?getSingletonPtr@WindowManager@CEGUI@@SAPAV12@XZ", &proc, error)) {
        return false;
    }
    cached.get_window_manager_singleton_ptr = reinterpret_cast<CeguiBindings::GetSingletonPtrFn>(proc);

    if (!ResolveBinding(module, "?injectMouseButtonDown@System@CEGUI@@QAE_NW4MouseButton@2@@Z", &proc, error)) {
        return false;
    }
    cached.inject_mouse_button_down = reinterpret_cast<CeguiBindings::InjectMouseButtonFn>(proc);

    if (!ResolveBinding(module, "?injectMouseButtonUp@System@CEGUI@@QAE_NW4MouseButton@2@@Z", &proc, error)) {
        return false;
    }
    cached.inject_mouse_button_up = reinterpret_cast<CeguiBindings::InjectMouseButtonFn>(proc);

    if (!ResolveBinding(module, "?injectMousePosition@System@CEGUI@@QAE_NMM@Z", &proc, error)) {
        return false;
    }
    cached.inject_mouse_position = reinterpret_cast<CeguiBindings::InjectMousePositionFn>(proc);

    if (!ResolveBinding(module, "?injectMouseWheelChange@System@CEGUI@@QAE_NM@Z", &proc, error)) {
        return false;
    }
    cached.inject_mouse_wheel_change = reinterpret_cast<CeguiBindings::InjectMouseWheelFn>(proc);

    if (!ResolveBinding(module, "?injectChar@System@CEGUI@@QAE_NI@Z", &proc, error)) {
        return false;
    }
    cached.inject_char = reinterpret_cast<CeguiBindings::InjectCharFn>(proc);

    if (!ResolveBinding(module, "?injectKeyDown@System@CEGUI@@QAE_NI@Z", &proc, error)) {
        return false;
    }
    cached.inject_key_down = reinterpret_cast<CeguiBindings::InjectKeyFn>(proc);

    if (!ResolveBinding(module, "?injectKeyUp@System@CEGUI@@QAE_NI@Z", &proc, error)) {
        return false;
    }
    cached.inject_key_up = reinterpret_cast<CeguiBindings::InjectKeyFn>(proc);

    if (!ResolveBinding(module, "?isWindowPresent@WindowManager@CEGUI@@QBE_NABVString@2@@Z", &proc, error)) {
        return false;
    }
    cached.window_manager_is_window_present =
        reinterpret_cast<CeguiBindings::WindowManagerIsWindowPresentFn>(proc);

    if (!ResolveBinding(module, "?getWindow@WindowManager@CEGUI@@QBEPAVWindow@2@ABVString@2@@Z", &proc, error)) {
        return false;
    }
    cached.window_manager_get_window =
        reinterpret_cast<CeguiBindings::WindowManagerGetWindowFn>(proc);

    if (!ResolveBinding(module, "?getName@Window@CEGUI@@QBEABVString@2@XZ", &proc, error)) {
        return false;
    }
    cached.window_get_name = reinterpret_cast<CeguiBindings::WindowGetStringFn>(proc);

    if (!ResolveBinding(module, "?getText@Window@CEGUI@@QBEABVString@2@XZ", &proc, error)) {
        return false;
    }
    cached.window_get_text = reinterpret_cast<CeguiBindings::WindowGetStringFn>(proc);

    if (!ResolveBinding(module, "?c_str@String@CEGUI@@QBEPBDXZ", &proc, error)) {
        return false;
    }
    cached.cegui_string_c_str = reinterpret_cast<CeguiBindings::CeguiStringCStrFn>(proc);

    if (!ResolveBinding(module, "?getChildCount@Window@CEGUI@@QBEIXZ", &proc, error)) {
        return false;
    }
    cached.window_get_child_count =
        reinterpret_cast<CeguiBindings::WindowGetChildCountFn>(proc);

    if (!ResolveBinding(module, "?getChildAtIdx@Window@CEGUI@@QBEPAV12@I@Z", &proc, error)) {
        return false;
    }
    cached.window_get_child_at_index =
        reinterpret_cast<CeguiBindings::WindowGetChildAtIdxFn>(proc);

    if (!ResolveBinding(module, "?getAbsoluteXPosition@Window@CEGUI@@QBEMXZ", &proc, error)) {
        return false;
    }
    cached.window_get_absolute_x = reinterpret_cast<CeguiBindings::WindowGetScalarFn>(proc);

    if (!ResolveBinding(module, "?getAbsoluteYPosition@Window@CEGUI@@QBEMXZ", &proc, error)) {
        return false;
    }
    cached.window_get_absolute_y = reinterpret_cast<CeguiBindings::WindowGetScalarFn>(proc);

    if (!ResolveBinding(module, "?getAbsoluteWidth@Window@CEGUI@@QBEMXZ", &proc, error)) {
        return false;
    }
    cached.window_get_absolute_width = reinterpret_cast<CeguiBindings::WindowGetScalarFn>(proc);

    if (!ResolveBinding(module, "?getAbsoluteHeight@Window@CEGUI@@QBEMXZ", &proc, error)) {
        return false;
    }
    cached.window_get_absolute_height = reinterpret_cast<CeguiBindings::WindowGetScalarFn>(proc);

    if (!ResolveBinding(module, "?isVisible@Window@CEGUI@@QBE_N_N@Z", &proc, error)) {
        return false;
    }
    cached.window_is_visible = reinterpret_cast<CeguiBindings::WindowIsFlagFn>(proc);

    if (!ResolveBinding(module, "?isDisabled@Window@CEGUI@@QBE_N_N@Z", &proc, error)) {
        return false;
    }
    cached.window_is_disabled = reinterpret_cast<CeguiBindings::WindowIsFlagFn>(proc);

    if (!ResolveBinding(module, "?isActive@Window@CEGUI@@QBE_NXZ", &proc, error)) {
        return false;
    }
    cached.window_is_active = reinterpret_cast<CeguiBindings::WindowIsActiveFn>(proc);

    if (!ResolveBinding(module, "?activate@Window@CEGUI@@QAEXXZ", &proc, error)) {
        return false;
    }
    cached.window_activate = reinterpret_cast<CeguiBindings::WindowActivateFn>(proc);

    if (!ResolveBinding(module, "?testClassName@Window@CEGUI@@QBE_NABVString@2@@Z", &proc, error)) {
        return false;
    }
    cached.window_test_class_name = reinterpret_cast<CeguiBindings::WindowTestClassNameFn>(proc);

    if (!ResolveBinding(module, "?hasInputFocus@Editbox@CEGUI@@QBE_NXZ", &proc, error)) {
        return false;
    }
    cached.editbox_has_input_focus =
        reinterpret_cast<CeguiBindings::WindowHasInputFocusFn>(proc);

    if (!ResolveBinding(module, "?hasInputFocus@MultiLineEditbox@CEGUI@@QBE_NXZ", &proc, error)) {
        return false;
    }
    cached.multiline_editbox_has_input_focus =
        reinterpret_cast<CeguiBindings::WindowHasInputFocusFn>(proc);

    if (!ResolveBinding(module, "?isReadOnly@Editbox@CEGUI@@QBE_NXZ", &proc, error)) {
        return false;
    }
    cached.editbox_is_read_only =
        reinterpret_cast<CeguiBindings::WindowIsReadOnlyFn>(proc);

    if (!ResolveBinding(module, "?isReadOnly@MultiLineEditbox@CEGUI@@QBE_NXZ", &proc, error)) {
        return false;
    }
    cached.multiline_editbox_is_read_only =
        reinterpret_cast<CeguiBindings::WindowIsReadOnlyFn>(proc);

    ResolveOptionalBinding(module, "?getFont@Window@CEGUI@@QBEPAVFont@2@XZ", &proc);
    cached.window_get_font = reinterpret_cast<CeguiBindings::WindowGetFontFn>(proc);

    if (!ResolveBinding(module, "?requestRedraw@Window@CEGUI@@QBEXXZ", &proc, error)) {
        return false;
    }
    cached.request_redraw = reinterpret_cast<CeguiBindings::RequestRedrawFn>(proc);

    if (!ResolveBinding(module, "?getWindowPosition@Window@CEGUI@@QBEABVUVector2@2@XZ", &proc, error)) {
        return false;
    }
    cached.window_get_window_position =
        reinterpret_cast<CeguiBindings::WindowGetWindowPositionFn>(proc);

    if (!ResolveBinding(module, "?setWindowPosition@Window@CEGUI@@QAEXABVUVector2@2@@Z", &proc, error)) {
        return false;
    }
    cached.window_set_window_position =
        reinterpret_cast<CeguiBindings::WindowSetWindowPositionFn>(proc);

    ResolveOptionalBinding(module, "?setProperty@Window@CEGUI@@QAEXABVString@2@0@Z", &proc);
    cached.window_set_property = reinterpret_cast<CeguiBindings::WindowSetPropertyFn>(proc);

    if (!ResolveBinding(module, "?getFont@FontManager@CEGUI@@QBEPAVFont@2@ABVString@2@@Z", &proc, error)) {
        return false;
    }
    cached.font_manager_get_font = reinterpret_cast<CeguiBindings::FontManagerGetFontFn>(proc);

    if (!ResolveBinding(module, "?isImagesetPresent@ImagesetManager@CEGUI@@QBE_NABVString@2@@Z", &proc, error)) {
        return false;
    }
    cached.imageset_manager_is_imageset_present =
        reinterpret_cast<CeguiBindings::ImagesetManagerIsImagesetPresentFn>(proc);

    if (!ResolveBinding(module, "?getImageset@ImagesetManager@CEGUI@@QBEPAVImageset@2@ABVString@2@@Z", &proc, error)) {
        return false;
    }
    cached.imageset_manager_get_imageset =
        reinterpret_cast<CeguiBindings::ImagesetManagerGetImagesetFn>(proc);

    if (!ResolveBinding(module, "?getTexture@Imageset@CEGUI@@QBEPAVTexture@2@XZ", &proc, error)) {
        return false;
    }
    cached.imageset_get_texture = reinterpret_cast<CeguiBindings::ImagesetGetTextureFn>(proc);

    if (!ResolveBinding(module, "?notifyScreenResolution@Font@CEGUI@@QAEXABVSize@2@@Z", &proc, error)) {
        return false;
    }
    cached.font_notify_screen_resolution =
        reinterpret_cast<CeguiBindings::FontNotifyScreenResolutionFn>(proc);

    if (!ResolveBinding(module, "?setNativeResolution@Font@CEGUI@@QAEXABVSize@2@@Z", &proc, error)) {
        return false;
    }
    cached.font_set_native_resolution =
        reinterpret_cast<CeguiBindings::FontSetNativeResolutionFn>(proc);

    if (!ResolveBinding(module, "?setAutoScalingEnabled@Font@CEGUI@@QAEX_N@Z", &proc, error)) {
        return false;
    }
    cached.font_set_auto_scaling_enabled =
        reinterpret_cast<CeguiBindings::FontSetAutoScalingEnabledFn>(proc);

    if (!ResolveBinding(module, "?getFontHeight@Font@CEGUI@@QBEMM@Z", &proc, error)) {
        return false;
    }
    cached.font_get_font_height =
        reinterpret_cast<CeguiBindings::FontGetFontHeightFn>(proc);

    if (!ResolveBinding(module, "?getLineSpacing@Font@CEGUI@@QBEMM@Z", &proc, error)) {
        return false;
    }
    cached.font_get_line_spacing =
        reinterpret_cast<CeguiBindings::FontGetLineSpacingFn>(proc);

    if (!ResolveBinding(module, "?getPointSize@Font@CEGUI@@QBEIXZ", &proc, error)) {
        return false;
    }
    cached.font_get_point_size =
        reinterpret_cast<CeguiBindings::FontGetPointSizeFn>(proc);

    ResolveOptionalBinding(module, "?getWrappedTextExtent@Font@CEGUI@@ABEMABVString@2@MM@Z", &proc);
    cached.font_get_wrapped_text_extent =
        reinterpret_cast<CeguiBindings::FontGetWrappedTextExtentFn>(proc);

    if (!ResolveBinding(module, "?createFontFromFT_Face@Font@CEGUI@@AAEXIII@Z", &proc, error)) {
        return false;
    }
    cached.font_create_font_from_ft_face =
        reinterpret_cast<CeguiBindings::FontCreateFromFtFaceFn>(proc);

    if (!ResolveBinding(module, "??0String@CEGUI@@QAE@PBD@Z", &proc, error)) {
        return false;
    }
    cached.cegui_string_ctor_from_ansi =
        reinterpret_cast<CeguiBindings::CeguiStringCtorFromAnsiFn>(proc);

    if (!ResolveBinding(module, "??1String@CEGUI@@QAE@XZ", &proc, error)) {
        return false;
    }
    cached.cegui_string_dtor = reinterpret_cast<CeguiBindings::CeguiStringDtorFn>(proc);

    if (!ResolveBinding(module, "??_7EventArgs@CEGUI@@6B@", &proc, error)) {
        return false;
    }
    cached.event_args_vftable = proc;

    if (!ResolveBinding(module, "?EventNamespace@Renderer@CEGUI@@2VString@2@B", &proc, error)) {
        return false;
    }
    cached.renderer_event_namespace = proc;

    if (!ResolveBinding(module, "?EventDisplaySizeChanged@Renderer@CEGUI@@2VString@2@B", &proc, error)) {
        return false;
    }
    cached.renderer_event_display_size_changed = proc;

    resolved = true;
    *out = cached;
    return true;
}

}  // namespace pal4::inject

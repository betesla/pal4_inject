#include "dialog_pagination_hooks.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "cegui_bindings.h"
#include "cegui_font_experiment.h"
#include "hook_logging.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using DialogHandleTextDisplayFn = void (__thiscall*)(void*);
using OlsNeedChangePageFn = bool (__thiscall*)(void*);
using OlsIsTypeEndFn = bool (__thiscall*)(void*);

constexpr std::ptrdiff_t kDialogEditboxOffset = 0x38;
constexpr std::ptrdiff_t kDialogTextFrameOffset = 0x754;
constexpr std::ptrdiff_t kDialogFramePageStartLineOffset = 0x100;
constexpr std::ptrdiff_t kDialogFrameCurrentLineOffset = 0x104;
constexpr std::ptrdiff_t kDialogFrameVisibleTopOffset = 0x110;
constexpr std::ptrdiff_t kDialogFrameVisibleBottomOffset = 0x114;
constexpr std::ptrdiff_t kDialogFrameNeedChangePageOffset = 0x120;
constexpr std::size_t kMaxPreviewBytes = 48;
constexpr char kOiramlookModuleName[] = "OIRAMLOOK.dll";
constexpr char kNeedChangePageSymbol[] = "?needChangePage@OLSDialogEditbox@CEGUI@@QBE_NXZ";
constexpr char kIsTypeEndSymbol[] = "?isTypeEnd@OLSDialogEditbox@CEGUI@@QBE_NXZ";
constexpr char kLineSpaceExtentProperty[] = "LineSpaceExtent";
constexpr char kScenarioDialogLineSpaceExtent[] = "0.000000";

DialogHandleTextDisplayFn g_original_dialog_handle_text_display = nullptr;
OlsNeedChangePageFn g_need_change_page = nullptr;
OlsIsTypeEndFn g_is_type_end = nullptr;
std::mutex g_line_space_fix_mutex;
std::unordered_set<void*> g_line_space_fixed_editboxes;

struct ScopedCeguiString {
    OpaqueCeguiString storage{};
    const CeguiBindings* bindings = nullptr;

    ~ScopedCeguiString() {
        if (bindings && bindings->cegui_string_dtor) {
            bindings->cegui_string_dtor(&storage);
        }
    }
};

struct DialogPaginationSnapshot {
    bool captured = false;
    std::size_t text_bytes = 0;
    std::string preview;
    float absolute_width = 0.0F;
    float absolute_height = 0.0F;
    float font_height = 0.0F;
    float line_spacing = 0.0F;
    float wrapped_extent = 0.0F;
    bool wrapped_extent_valid = false;
    bool pre_is_type_end = false;
    bool pre_need_change_page = false;
    bool pre_need_change_page_valid = false;
    bool post_is_type_end = false;
    bool post_need_change_page = false;
    bool post_need_change_page_valid = false;
    std::uint32_t page_start_line = 0;
    std::uint32_t current_line = 0;
    float visible_top = 0.0F;
    float visible_bottom = 0.0F;
    bool frame_need_change_page = false;
    bool frame_state_valid = false;
};

void EnsureOlsBindingsResolved() {
    if (g_need_change_page && g_is_type_end) {
        return;
    }
    const HMODULE module = GetModuleHandleA(kOiramlookModuleName);
    if (!module) {
        return;
    }
    if (!g_need_change_page) {
        g_need_change_page = reinterpret_cast<OlsNeedChangePageFn>(
            GetProcAddress(module, kNeedChangePageSymbol));
    }
    if (!g_is_type_end) {
        g_is_type_end = reinterpret_cast<OlsIsTypeEndFn>(
            GetProcAddress(module, kIsTypeEndSymbol));
    }
}

bool BuildCeguiAnsiString(
    const CeguiBindings& bindings,
    const char* text,
    ScopedCeguiString* out) {
    if (!text || !out || !bindings.cegui_string_ctor_from_ansi || !bindings.cegui_string_dtor) {
        return false;
    }
    bindings.cegui_string_ctor_from_ansi(&out->storage, text);
    out->bindings = &bindings;
    return true;
}

std::string TruncatePreview(std::string text) {
    std::replace(text.begin(), text.end(), '\r', ' ');
    std::replace(text.begin(), text.end(), '\n', ' ');
    if (text.size() > kMaxPreviewBytes) {
        text.resize(kMaxPreviewBytes);
        text += "...";
    }
    return text;
}

bool RememberLineSpaceFixedEditbox(void* editbox) {
    std::scoped_lock lock(g_line_space_fix_mutex);
    return g_line_space_fixed_editboxes.insert(editbox).second;
}

void ApplyScenarioDialogLineSpaceFix(
    void* editbox,
    const CeguiBindings& bindings) {
    if (!editbox || !bindings.window_set_property) {
        return;
    }
    ScopedCeguiString property_name{};
    ScopedCeguiString property_value{};
    if (!BuildCeguiAnsiString(bindings, kLineSpaceExtentProperty, &property_name) ||
        !BuildCeguiAnsiString(bindings, kScenarioDialogLineSpaceExtent, &property_value)) {
        return;
    }

    bindings.window_set_property(editbox, &property_name.storage, &property_value.storage);
    if (bindings.request_redraw) {
        bindings.request_redraw(editbox);
    }

    if (RememberLineSpaceFixedEditbox(editbox)) {
        AppendHookEventLog(
            HookId::dialog_handle_text_display,
            std::string("hook=dialog_line_space_fix property=LineSpaceExtent value=") +
                kScenarioDialogLineSpaceExtent);
    }
}

void* GetDialogEditbox(void* dialog_object) {
    if (!dialog_object) {
        return nullptr;
    }
    return *reinterpret_cast<void**>(
        static_cast<std::uint8_t*>(dialog_object) + kDialogEditboxOffset);
}

void CaptureDialogTextFrameState(void* editbox, DialogPaginationSnapshot* snapshot) {
    if (!editbox || !snapshot) {
        return;
    }

    const auto* frame = static_cast<const std::uint8_t*>(editbox) + kDialogTextFrameOffset;
    snapshot->page_start_line =
        *reinterpret_cast<const std::uint32_t*>(frame + kDialogFramePageStartLineOffset);
    snapshot->current_line =
        *reinterpret_cast<const std::uint32_t*>(frame + kDialogFrameCurrentLineOffset);
    snapshot->visible_top =
        *reinterpret_cast<const float*>(frame + kDialogFrameVisibleTopOffset);
    snapshot->visible_bottom =
        *reinterpret_cast<const float*>(frame + kDialogFrameVisibleBottomOffset);
    snapshot->frame_need_change_page =
        *reinterpret_cast<const bool*>(frame + kDialogFrameNeedChangePageOffset);
    snapshot->frame_state_valid = true;
}

void ApplyScenarioDialogLineSpaceFixForDialog(void* dialog_object) {
    std::string height_hook_error;
    EnsureDialogRichTextGlyphHeightCompensationHook(&height_hook_error);

    void* editbox = GetDialogEditbox(dialog_object);
    if (!editbox) {
        return;
    }

    CeguiBindings bindings{};
    std::string error;
    if (!TryGetCeguiBindings(&bindings, &error)) {
        return;
    }

    ApplyScenarioDialogLineSpaceFix(editbox, bindings);
}

DialogPaginationSnapshot CaptureDialogPaginationSnapshot(void* dialog_object) {
    DialogPaginationSnapshot snapshot{};
    EnsureOlsBindingsResolved();
    void* editbox = GetDialogEditbox(dialog_object);
    if (!editbox) {
        return snapshot;
    }

    CeguiBindings bindings{};
    std::string error;
    if (!TryGetCeguiBindings(&bindings, &error)) {
        return snapshot;
    }

    snapshot.captured = true;
    if (g_is_type_end) {
        snapshot.pre_is_type_end = g_is_type_end(editbox);
    }
    if (g_need_change_page) {
        snapshot.pre_need_change_page = g_need_change_page(editbox);
        snapshot.pre_need_change_page_valid = true;
    }
    CaptureDialogTextFrameState(editbox, &snapshot);
    if (bindings.window_get_absolute_width) {
        snapshot.absolute_width = bindings.window_get_absolute_width(editbox);
    }
    if (bindings.window_get_absolute_height) {
        snapshot.absolute_height = bindings.window_get_absolute_height(editbox);
    }

    const OpaqueCeguiString* text = nullptr;
    if (bindings.window_get_text) {
        text = bindings.window_get_text(editbox);
    }
    if (bindings.cegui_string_c_str && text) {
        const char* raw = bindings.cegui_string_c_str(text);
        if (raw) {
            snapshot.text_bytes = std::char_traits<char>::length(raw);
            snapshot.preview = TruncatePreview(std::string(raw));
        }
    }

    if (bindings.window_get_font && bindings.font_get_font_height) {
        void* font = bindings.window_get_font(editbox);
        if (font) {
            snapshot.font_height = bindings.font_get_font_height(font, 1.0F);
            if (bindings.font_get_line_spacing) {
                snapshot.line_spacing = bindings.font_get_line_spacing(font, 1.0F);
            }
            if (bindings.font_get_wrapped_text_extent && text) {
                snapshot.wrapped_extent = bindings.font_get_wrapped_text_extent(
                    font,
                    text,
                    snapshot.absolute_width,
                    1.0F);
                snapshot.wrapped_extent_valid = true;
            }
        }
    }

    return snapshot;
}

void CapturePostDialogPaginationState(
    void* dialog_object,
    DialogPaginationSnapshot* snapshot) {
    if (!snapshot || !dialog_object) {
        return;
    }
    EnsureOlsBindingsResolved();
    auto* editbox = *reinterpret_cast<void**>(
        static_cast<std::uint8_t*>(dialog_object) + kDialogEditboxOffset);
    if (!editbox) {
        return;
    }
    if (g_is_type_end) {
        snapshot->post_is_type_end = g_is_type_end(editbox);
    }
    if (g_need_change_page) {
        snapshot->post_need_change_page = g_need_change_page(editbox);
        snapshot->post_need_change_page_valid = true;
    }
    CaptureDialogTextFrameState(editbox, snapshot);
}

std::string BuildDialogPaginationLogLine(const DialogPaginationSnapshot& before, const DialogPaginationSnapshot& after) {
    std::ostringstream out;
    out << "hook=dialog_handle_text_display";
    if (before.captured) {
        out
            << " before_bytes=" << before.text_bytes
            << " before_box=" << before.absolute_width << "x" << before.absolute_height
            << " before_font_height=" << before.font_height
            << " before_line_spacing=" << before.line_spacing
            << " pre_is_type_end=" << (before.pre_is_type_end ? 1 : 0);
        if (before.pre_need_change_page_valid) {
            out << " pre_need_change_page=" << (before.pre_need_change_page ? 1 : 0);
        }
        if (before.frame_state_valid) {
            out
                << " before_page_start_line=" << before.page_start_line
                << " before_current_line=" << before.current_line
                << " before_visible=" << before.visible_top << ".." << before.visible_bottom
                << " before_frame_need_change_page=" << (before.frame_need_change_page ? 1 : 0);
        }
        if (before.wrapped_extent_valid) {
            out << " before_wrapped_extent=" << before.wrapped_extent;
        }
        if (!before.preview.empty()) {
            out << " before_text=\"" << before.preview << "\"";
        }
    }
    if (after.captured) {
        out
            << " after_bytes=" << after.text_bytes
            << " after_box=" << after.absolute_width << "x" << after.absolute_height
            << " after_font_height=" << after.font_height
            << " after_line_spacing=" << after.line_spacing
            << " post_is_type_end=" << (after.post_is_type_end ? 1 : 0);
        if (after.post_need_change_page_valid) {
            out << " post_need_change_page=" << (after.post_need_change_page ? 1 : 0);
        }
        if (after.frame_state_valid) {
            out
                << " after_page_start_line=" << after.page_start_line
                << " after_current_line=" << after.current_line
                << " after_visible=" << after.visible_top << ".." << after.visible_bottom
                << " after_frame_need_change_page=" << (after.frame_need_change_page ? 1 : 0);
        }
        if (after.wrapped_extent_valid) {
            out << " after_wrapped_extent=" << after.wrapped_extent;
        }
        if (!after.preview.empty()) {
            out << " after_text=\"" << after.preview << "\"";
        }
    }
    return out.str();
}

void __fastcall Hook_DialogHandleTextDisplay(void* dialog_object, void*) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::dialog_handle_text_display);

    if (!g_original_dialog_handle_text_display) {
        const std::string error = "original dialog_HandleTextDisplay trampoline is null";
        state.SetHookError(HookId::dialog_handle_text_display, error);
        state.SetLastError(error);
        return;
    }

    DialogPaginationSnapshot before{};
    if (ShouldEmitHookLog(HookId::dialog_handle_text_display)) {
        before = CaptureDialogPaginationSnapshot(dialog_object);
    }
    ApplyScenarioDialogLineSpaceFixForDialog(dialog_object);

    g_original_dialog_handle_text_display(dialog_object);
    state.ClearHookError(HookId::dialog_handle_text_display);

    if (!ShouldEmitHookLog(HookId::dialog_handle_text_display)) {
        return;
    }

    DialogPaginationSnapshot after = CaptureDialogPaginationSnapshot(dialog_object);
    CapturePostDialogPaginationState(dialog_object, &after);
    GetRuntimeState().AppendEventLog(BuildDialogPaginationLogLine(before, after));
}

}  // namespace

void* GetDialogPaginationReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::dialog_handle_text_display:
        return reinterpret_cast<void*>(&Hook_DialogHandleTextDisplay);
    default:
        return nullptr;
    }
}

void SetDialogPaginationOriginalTrampoline(const HookId id, void* trampoline) {
    switch (id) {
    case HookId::dialog_handle_text_display:
        g_original_dialog_handle_text_display =
            reinterpret_cast<DialogHandleTextDisplayFn>(trampoline);
        break;
    default:
        break;
    }
}

}  // namespace pal4::inject

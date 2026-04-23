#include "ui_snapshot_runtime.h"

#include <cmath>
#include <mutex>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "cegui_bindings.h"
#include "input_hooks.h"
#include "pal4inject/cegui_widescreen.h"
#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

std::mutex g_snapshot_cache_mutex;
UiSnapshotTree g_snapshot_cache{};
bool g_have_snapshot_cache = false;

std::string CopyCeguiString(
    const CeguiBindings& bindings,
    const OpaqueCeguiString* text) {
    if (!text || !bindings.cegui_string_c_str) {
        return {};
    }
    const char* chars = bindings.cegui_string_c_str(text);
    return chars ? std::string(chars) : std::string();
}

bool WindowMatchesClass(
    const CeguiBindings& bindings,
    const void* window,
    const char* class_name) {
    if (!window || !class_name || !bindings.window_test_class_name ||
        !bindings.cegui_string_ctor_from_ansi || !bindings.cegui_string_dtor) {
        return false;
    }

    OpaqueCeguiString query{};
    bindings.cegui_string_ctor_from_ansi(&query, class_name);
    const bool result = bindings.window_test_class_name(window, &query);
    bindings.cegui_string_dtor(&query);
    return result;
}

std::uintptr_t MainModuleBase() {
    auto& state = GetRuntimeState();
    std::uintptr_t base = state.MainModuleBase();
    if (base == 0) {
        base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
        state.SetMainModuleBase(base);
    }
    return base;
}

void* ResolveRuntimeData(const std::uint32_t ida_ea) {
    const auto base = MainModuleBase();
    if (base == 0) {
        return nullptr;
    }
    return reinterpret_cast<void*>(ida::ResolveRuntimeAddress(base, ida_ea));
}

int* ReadGameConfigPointer() {
    auto* config_ptr_address =
        static_cast<int**>(ResolveRuntimeData(ida::kGameConfigGlobal));
    return config_ptr_address ? *config_ptr_address : nullptr;
}

void ConvertLogicalToRawClientPoint(
    const std::int32_t logical_x,
    const std::int32_t logical_y,
    std::int32_t* out_x,
    std::int32_t* out_y) {
    if (!out_x || !out_y) {
        return;
    }

    *out_x = logical_x;
    *out_y = logical_y;

    const int* config = ReadGameConfigPointer();
    if (!config) {
        return;
    }

    const auto plan = BuildCeguiWidescreenPlan(config[0], config[1]);
    if (!plan.apply || plan.use_original_variant || plan.uniform_scale <= 0.0F) {
        return;
    }

    *out_x = static_cast<std::int32_t>(std::lround(
        ProjectWidescreenLogicalXToPhysicalPixels(plan, static_cast<float>(logical_x))));
    *out_y = static_cast<std::int32_t>(std::lround(
        static_cast<float>(logical_y) * plan.uniform_scale));
}

std::string ClassifyWindowType(
    const CeguiBindings& bindings,
    const void* window) {
    if (WindowMatchesClass(bindings, window, "GUISheet")) {
        return "gui_sheet";
    }
    if (WindowMatchesClass(bindings, window, "PushButton")) {
        return "button";
    }
    if (WindowMatchesClass(bindings, window, "Editbox")) {
        return "editbox";
    }
    if (WindowMatchesClass(bindings, window, "MultiLineEditbox")) {
        return "multiline_editbox";
    }
    if (WindowMatchesClass(bindings, window, "FrameWindow")) {
        return "frame_window";
    }
    if (WindowMatchesClass(bindings, window, "Combobox")) {
        return "combobox";
    }
    if (WindowMatchesClass(bindings, window, "StaticText")) {
        return "static_text";
    }
    return "window";
}

bool IsEditableWindow(
    const CeguiBindings& bindings,
    const void* window) {
    if (WindowMatchesClass(bindings, window, "Editbox") && bindings.editbox_is_read_only) {
        return !bindings.editbox_is_read_only(window);
    }
    if (WindowMatchesClass(bindings, window, "MultiLineEditbox") &&
        bindings.multiline_editbox_is_read_only) {
        return !bindings.multiline_editbox_is_read_only(window);
    }
    return false;
}

bool IsFocusedWindow(
    const CeguiBindings& bindings,
    const void* window) {
    if (WindowMatchesClass(bindings, window, "Editbox") &&
        bindings.editbox_has_input_focus) {
        return bindings.editbox_has_input_focus(window);
    }
    if (WindowMatchesClass(bindings, window, "MultiLineEditbox") &&
        bindings.multiline_editbox_has_input_focus) {
        return bindings.multiline_editbox_has_input_focus(window);
    }
    return bindings.window_is_active ? bindings.window_is_active(window) : false;
}

std::string PathSegmentForNode(
    const std::string_view name,
    const std::size_t child_index) {
    if (!name.empty()) {
        return std::string(name);
    }
    return "unnamed_" + std::to_string(child_index);
}

bool CaptureUiSnapshotNode(
    const CeguiBindings& bindings,
    const void* window,
    const std::string_view parent_path,
    const std::size_t child_index,
    std::uint32_t* next_ref,
    UiSnapshotNode* out,
    std::string* error) {
    if (!window || !out || !next_ref) {
        if (error) {
            *error = "ui snapshot node input is null";
        }
        return false;
    }

    out->ref = "e" + std::to_string((*next_ref)++);
    out->name = bindings.window_get_name
        ? CopyCeguiString(bindings, bindings.window_get_name(window))
        : std::string();
    out->text = bindings.window_get_text
        ? CopyCeguiString(bindings, bindings.window_get_text(window))
        : std::string();
    out->type = ClassifyWindowType(bindings, window);
    const std::string path_segment = PathSegmentForNode(out->name, child_index);
    out->path = parent_path.empty()
        ? path_segment
        : std::string(parent_path) + "/" + path_segment;
    out->visible = bindings.window_is_visible
        ? bindings.window_is_visible(window, false)
        : false;
    out->enabled = bindings.window_is_disabled
        ? !bindings.window_is_disabled(window, false)
        : false;
    out->focused = IsFocusedWindow(bindings, window);
    out->editable = IsEditableWindow(bindings, window);
    const float left = bindings.window_get_absolute_x ? bindings.window_get_absolute_x(window) : 0.0F;
    const float top = bindings.window_get_absolute_y ? bindings.window_get_absolute_y(window) : 0.0F;
    const float width = bindings.window_get_absolute_width ? bindings.window_get_absolute_width(window) : 0.0F;
    const float height = bindings.window_get_absolute_height ? bindings.window_get_absolute_height(window) : 0.0F;
    out->rect.left = static_cast<std::int32_t>(std::lround(left));
    out->rect.top = static_cast<std::int32_t>(std::lround(top));
    out->rect.right = static_cast<std::int32_t>(std::lround(left + width));
    out->rect.bottom = static_cast<std::int32_t>(std::lround(top + height));
    out->clickable =
        out->visible &&
        out->enabled &&
        out->rect.right > out->rect.left &&
        out->rect.bottom > out->rect.top &&
        out->type != "gui_sheet";

    const unsigned int child_count = bindings.window_get_child_count
        ? bindings.window_get_child_count(window)
        : 0;
    out->children.clear();
    out->children.reserve(child_count);
    for (unsigned int i = 0; i < child_count; ++i) {
        void* child = bindings.window_get_child_at_index
            ? bindings.window_get_child_at_index(window, i)
            : nullptr;
        if (!child) {
            continue;
        }
        UiSnapshotNode child_node{};
        if (!CaptureUiSnapshotNode(
                bindings,
                child,
                out->path,
                i,
                next_ref,
                &child_node,
                error)) {
            return false;
        }
        out->children.push_back(std::move(child_node));
    }

    return true;
}

bool CaptureUiSnapshotInternal(
    const bool update_cache,
    UiSnapshotTree* out,
    std::string* error) {
    CeguiBindings bindings{};
    if (!TryGetCeguiBindings(&bindings, error)) {
        return false;
    }

    void* system = bindings.get_system_singleton_ptr
        ? bindings.get_system_singleton_ptr()
        : nullptr;
    if (!system) {
        if (error) {
            *error = "CEGUI system singleton is null";
        }
        return false;
    }

    void* gui_sheet = bindings.get_gui_sheet
        ? bindings.get_gui_sheet(system)
        : nullptr;
    if (!gui_sheet) {
        if (error) {
            *error = "CEGUI gui sheet is null";
        }
        return false;
    }

    UiSnapshotTree tree{};
    std::uint32_t next_ref = 1;
    if (!CaptureUiSnapshotNode(bindings, gui_sheet, {}, 0, &next_ref, &tree.root, error)) {
        return false;
    }

    if (update_cache) {
        std::scoped_lock lock(g_snapshot_cache_mutex);
        g_snapshot_cache = tree;
        g_have_snapshot_cache = true;
    }
    if (out) {
        *out = std::move(tree);
    }
    return true;
}

bool LookupCachedUiSnapshotNodeInternal(
    const std::string_view ref,
    UiSnapshotNode* out,
    std::string* error) {
    std::scoped_lock lock(g_snapshot_cache_mutex);
    if (!g_have_snapshot_cache) {
        if (error) {
            *error = "ui snapshot cache is empty";
        }
        return false;
    }

    const auto* node = FindUiSnapshotNodeByRef(g_snapshot_cache, ref);
    if (!node) {
        if (error) {
            *error = "stale ui ref";
        }
        return false;
    }
    if (out) {
        *out = *node;
    }
    return true;
}

std::uint32_t PackClientPoint(const std::int32_t x, const std::int32_t y) {
    return static_cast<std::uint32_t>((y & 0xFFFF) << 16) |
        static_cast<std::uint32_t>(x & 0xFFFF);
}

bool DispatchClientClick(
    const UiSnapshotNode& node,
    std::string* error) {
    if (!node.clickable) {
        if (error) {
            *error = "ui ref is not clickable";
        }
        return false;
    }

    const std::int32_t logical_center_x = (node.rect.left + node.rect.right) / 2;
    const std::int32_t logical_center_y = (node.rect.top + node.rect.bottom) / 2;
    std::int32_t raw_center_x = logical_center_x;
    std::int32_t raw_center_y = logical_center_y;
    ConvertLogicalToRawClientPoint(
        logical_center_x,
        logical_center_y,
        &raw_center_x,
        &raw_center_y);
    const std::uint32_t lparam = PackClientPoint(raw_center_x, raw_center_y);
    UiMessageCommand command{};
    command.bypass_os_queue = true;

    command.msg = WM_MOUSEMOVE;
    command.wparam = 0;
    command.lparam = lparam;
    std::string move_error;
    if (!DispatchUiMessageCommand(command, &move_error)) {
        command.bypass_os_queue = false;
        std::string ignored_fallback_error;
        DispatchUiMessageCommand(command, &ignored_fallback_error);
        command.bypass_os_queue = true;
    }

    command.msg = WM_LBUTTONDOWN;
    command.wparam = MK_LBUTTON;
    std::string down_error;
    if (!DispatchUiMessageCommand(command, &down_error)) {
        command.bypass_os_queue = false;
        if (!DispatchUiMessageCommand(command, error)) {
            return false;
        }
        command.bypass_os_queue = true;
    }

    command.msg = WM_LBUTTONUP;
    command.wparam = 0;
    std::string up_error;
    if (!DispatchUiMessageCommand(command, &up_error)) {
        command.bypass_os_queue = false;
        if (!DispatchUiMessageCommand(command, error)) {
            return false;
        }
    }
    return true;
}

bool DispatchTextInput(
    const std::string_view text,
    std::string* error) {
    for (const unsigned char ch : text) {
        UiMessageCommand command{};
        command.msg = WM_CHAR;
        command.wparam = ch;
        command.lparam = 0;
        command.bypass_os_queue = true;
        if (!DispatchUiMessageCommand(command, error)) {
            return false;
        }
    }
    return true;
}

const UiSnapshotNode* FindFocusedEditableNode(const UiSnapshotNode& node) {
    if (node.focused && node.editable) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const auto* found = FindFocusedEditableNode(child)) {
            return found;
        }
    }
    return nullptr;
}

bool ContainsInsensitiveAscii(
    const std::string_view haystack,
    const std::string_view needle) {
    if (needle.empty() || haystack.size() < needle.size()) {
        return false;
    }
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            char lhs = haystack[i + j];
            char rhs = needle[j];
            if (lhs >= 'A' && lhs <= 'Z') {
                lhs = static_cast<char>(lhs - 'A' + 'a');
            }
            if (rhs >= 'A' && rhs <= 'Z') {
                rhs = static_cast<char>(rhs - 'A' + 'a');
            }
            if (lhs != rhs) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

const UiSnapshotNode* FindNodeByNameInsensitive(
    const UiSnapshotNode& node,
    const std::string_view name) {
    if (ContainsInsensitiveAscii(node.name, name) && node.name.size() == name.size()) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const auto* found = FindNodeByNameInsensitive(child, name)) {
            return found;
        }
    }
    return nullptr;
}

}  // namespace

bool CaptureAndCacheUiSnapshot(UiSnapshotTree* out, std::string* error) {
    return CaptureUiSnapshotInternal(true, out, error);
}

bool CopyCachedUiSnapshotNode(
    const std::string_view ref,
    UiSnapshotNode* out,
    std::string* error) {
    return LookupCachedUiSnapshotNodeInternal(ref, out, error);
}

bool ClickCachedUiSnapshotRef(const std::string_view ref, std::string* error) {
    UiSnapshotNode node{};
    if (!LookupCachedUiSnapshotNodeInternal(ref, &node, error)) {
        return false;
    }
    return DispatchClientClick(node, error);
}

bool ClickLikelySystemMenuCloseButton(std::string* error) {
    UiSnapshotTree tree{};
    if (!CaptureUiSnapshotInternal(false, &tree, error)) {
        return false;
    }

    const UiSnapshotNode* close_button = FindNodeByNameInsensitive(tree.root, "btnClose");
    if (!close_button) {
        if (error) {
            *error = "failed to locate system-menu close button btnClose";
        }
        return false;
    }
    return DispatchClientClick(*close_button, error);
}

bool FillCachedUiSnapshotRef(
    const std::string_view ref,
    const std::string_view text,
    std::string* error) {
    UiSnapshotNode node{};
    if (!LookupCachedUiSnapshotNodeInternal(ref, &node, error)) {
        return false;
    }
    if (!node.editable) {
        if (error) {
            *error = "ui ref is not editable";
        }
        return false;
    }
    if (!DispatchClientClick(node, error)) {
        return false;
    }
    return DispatchTextInput(text, error);
}

bool TypeIntoFocusedUiWindow(const std::string_view text, std::string* error) {
    UiSnapshotTree tree{};
    if (!CaptureUiSnapshotInternal(false, &tree, error)) {
        return false;
    }

    const auto* focused = FindFocusedEditableNode(tree.root);
    if (!focused) {
        if (error) {
            *error = "focused editable ui window was not found";
        }
        return false;
    }
    return DispatchTextInput(text, error);
}

}  // namespace pal4::inject

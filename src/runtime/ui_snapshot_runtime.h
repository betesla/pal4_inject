#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "pal4inject/ui_snapshot.h"

namespace pal4::inject {

struct SystemMenuUiState {
    bool visible = false;
    bool menu_navigation_visible = false;
};

struct GamepadNavigationUiState {
    SystemMenuUiState menu{};
    bool combat_navigation_visible = false;
    bool combat_action_wheel_visible = false;
};

struct CombatNavigationUiState {
    bool visible = false;
    bool action_wheel_visible = false;
};

enum class CombatActionButton : std::uint8_t {
    attack = 0,
    defend,
};

struct SystemMenuNavigationState {
    std::array<bool, 7> main_pages{};
    std::array<bool, 6> sub_pages{};
    std::uint32_t current_main_page = 0;
    bool has_current_main_page = false;
};

bool CaptureAndCacheUiSnapshot(UiSnapshotTree* out, std::string* error);
bool CopyCachedUiSnapshotNode(
    std::string_view ref,
    UiSnapshotNode* out,
    std::string* error);
bool ClickCachedUiSnapshotRef(std::string_view ref, std::string* error);
bool QuerySystemMenuState(
    SystemMenuUiState* state,
    std::string* error);
bool QueryGamepadNavigationUiState(
    GamepadNavigationUiState* state,
    std::string* error);
bool QueryCombatNavigationUiState(
    CombatNavigationUiState* state,
    std::string* error);
bool TryActivateCombatActionButton(
    CombatActionButton button,
    std::string* error);
bool QuerySystemMenuNavigationState(
    SystemMenuNavigationState* state,
    std::string* error);
bool QuerySystemMenuShellVisible(bool* visible, std::string* error);
bool TryActivateSystemMenuRoleSwitch(bool next, std::string* error);
bool FillCachedUiSnapshotRef(
    std::string_view ref,
    std::string_view text,
    std::string* error);
bool TypeIntoFocusedUiWindow(std::string_view text, std::string* error);

}  // namespace pal4::inject

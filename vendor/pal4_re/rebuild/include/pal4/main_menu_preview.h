#pragma once

#include <optional>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace pal4 {

enum class MainMenuPreviewAction {
    none,
    new_game,
    load_game,
    exit_game,
};

struct MainMenuPreviewButton {
    MainMenuPreviewAction action = MainMenuPreviewAction::none;
    std::string window_name;
    RECT rect{};
};

struct MainMenuPreviewSnapshot {
    bool preview_only = true;
    bool proves_real_main_menu = false;
    bool pointer_interaction_enabled = false;
    bool assets_ready = false;
    bool rendered = false;
    bool has_new_game = false;
    bool has_load_game = false;
    bool has_exit = false;
    std::vector<MainMenuPreviewButton> buttons;
};

const char* DescribeMainMenuPreviewBoundary(const MainMenuPreviewSnapshot& snapshot) noexcept;

class MainMenuPreviewRenderer {
public:
    MainMenuPreviewRenderer();
    ~MainMenuPreviewRenderer();

    bool Initialize();
    bool IsReady() const noexcept;
    MainMenuPreviewSnapshot Snapshot(const RECT& target_rect) const;
    void Render(HDC hdc, const RECT& target_rect);
    bool SavePreviewPng(const std::wstring& path, int width, int height);
    bool SavePreviewBmp(const std::wstring& path, int width, int height);
    std::optional<MainMenuPreviewAction> HitTest(POINT pt, const RECT& target_rect) const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace pal4

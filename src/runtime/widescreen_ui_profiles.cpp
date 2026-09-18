#include "widescreen_ui_profiles.h"

#include <array>

namespace pal4::inject {
namespace {

using Mode = WidescreenUiHorizontalMode;
using Policy = WidescreenUiPillarboxPolicy;
using Rule = WidescreenUiWindowRule;

constexpr auto kMinimapRules = std::to_array<Rule>({
    {"minimap_root", "minimap/Root", "", Mode::left_edge, false},
});

constexpr auto kPortraitRules = std::to_array<Rule>({
    {"portrait_root", "portrait/zhujiemian", "", Mode::right_edge, false},
    {"portrait_jump_hint", "portrait/ImgJumpHint", "ImgJumpHint", Mode::right_edge, false},
    {"portrait_timer", "portrait/StaticTimer", "StaticTimer", Mode::right_edge, false},
});

constexpr auto kMainWindowRules = std::to_array<Rule>({
    {"top_left", "MainWindow/zuoshang", "", Mode::left_edge, true},
    {"top_center", "MainWindow/shangzhong", "", Mode::stretch_between_edges, true},
    {"top_right", "MainWindow/youshang", "", Mode::right_edge, true},
    {"right_center", "MainWindow/youzhong", "", Mode::right_edge, true},
    {"bottom_right_primary", "MainWindow/youxia1", "", Mode::right_edge, true},
    {"bottom_right_alternate", "MainWindow/youxia", "", Mode::right_edge, true},
    {"bottom_center", "MainWindow/xia", "", Mode::stretch_between_edges, true},
    {"bottom_left_connector", "MainWindow/xia2", "", Mode::left_edge, true},
    {"bottom_left", "MainWindow/zuoxia", "", Mode::left_edge, true},
    {"left_center", "MainWindow/zuozhong", "", Mode::left_edge, true},
    {"new_game_button", "MainWindow/BtnNewGame", "", Mode::left_edge, true},
    {"load_game_button", "MainWindow/BtnLoadGame", "", Mode::left_edge, true},
    {"exit_button", "MainWindow/BtnExit", "", Mode::left_edge, true},
    {"movie_button", "MainWindow/BtnMovie", "", Mode::right_edge, true},
    {"information_button", "MainWindow/BtnInfomation", "", Mode::right_edge, true},
    {"about_button", "MainWindow/BtnAboutPal", "", Mode::right_edge, true},
    {"quest_button", "MainWindow/BtnQuest", "", Mode::right_edge, true},
    {"picture_button", "MainWindow/BtnPicture", "", Mode::right_edge, true},
});

constexpr auto kMoviePreviewRules = std::to_array<Rule>({
    {"top_left", "moviePreviewWindow/zuoshang", "", Mode::left_edge, true},
    {"top_center", "moviePreviewWindow/shang", "", Mode::stretch_between_edges, true},
    {"top_right", "moviePreviewWindow/youshang", "", Mode::right_edge, true},
    {"right_center", "moviePreviewWindow/youzhong", "", Mode::right_edge, true},
    {"bottom_right", "moviePreviewWindow/youxia", "", Mode::right_edge, true},
    {"bottom_center", "moviePreviewWindow/xia", "", Mode::stretch_between_edges, true},
    {"bottom_left", "moviePreviewWindow/zuoxia", "", Mode::left_edge, true},
    {"left_center", "moviePreviewWindow/zuozhong", "", Mode::left_edge, true},
    {"close_button", "moviePreviewWindow/BtnClose", "", Mode::right_edge, true},
});

constexpr auto kMoviePlaybackRules = std::to_array<Rule>({
    {"top_border", "MoviePlayWindow/shang", "", Mode::stretch_between_edges, true},
    {"left_border", "MoviePlayWindow/zuo", "", Mode::left_edge, true},
    {"right_border", "MoviePlayWindow/you", "", Mode::right_edge, true},
    {"bottom_border", "MoviePlayWindow/xia", "", Mode::stretch_between_edges, true},
    {"button_frame", "MoviePlayWindow/anniukuang", "", Mode::right_edge, true},
});

constexpr auto kLoadWindowRules = std::to_array<Rule>({
    {"top_left", "loadWindow/zuoshang", "", Mode::left_edge, true},
    {"top_center", "loadWindow/shang", "", Mode::stretch_between_edges, true},
    {"top_right", "loadWindow/youshang", "", Mode::right_edge, true},
    {"right_center", "loadWindow/youzhong", "", Mode::right_edge, true},
    {"bottom_right", "loadWindow/youxia", "", Mode::right_edge, true},
    {"bottom_center", "loadWindow/xia", "", Mode::stretch_between_edges, true},
    {"bottom_left", "loadWindow/zuoxia", "", Mode::left_edge, true},
    {"left_center", "loadWindow/zuozhong", "", Mode::left_edge, true},
});

// The save list and its page buttons remain centered at their native size.
// Only extend the full-screen tint and move the corner frame (with BtnClose).
constexpr auto kSaveWindowRules = std::to_array<Rule>({
    {"background", "SaveWindow/dise", "", Mode::stretch_between_edges, true},
    {"bottom_right", "SaveWindow/youxia", "", Mode::right_edge, true},
});

constexpr auto kPicturePreviewRules = std::to_array<Rule>({
    {"top_left", "picturePreviewWindow/zuoshang", "", Mode::left_edge, true},
    {"top_center", "picturePreviewWindow/shang", "", Mode::stretch_between_edges, true},
    {"top_right", "picturePreviewWindow/youshang", "", Mode::right_edge, true},
    {"right_center", "picturePreviewWindow/youzhong", "", Mode::right_edge, true},
    {"bottom_right", "picturePreviewWindow/youxia", "", Mode::right_edge, true},
    {"bottom_center", "picturePreviewWindow/xia", "", Mode::stretch_between_edges, true},
    {"bottom_left", "picturePreviewWindow/zuoxia", "", Mode::left_edge, true},
    {"left_center", "picturePreviewWindow/zuozhong", "", Mode::left_edge, true},
    {"close_button", "picturePreviewWindow/BtnClose", "", Mode::right_edge, true},
});

constexpr auto kPictureViewRules = std::to_array<Rule>({
    {"top_left", "PictureViewWindow/zuoshang", "", Mode::left_edge, true},
    {"top_center", "PictureViewWindow/shang", "", Mode::stretch_between_edges, true},
    {"top_right", "PictureViewWindow/youshang", "", Mode::right_edge, true},
    {"right_center", "PictureViewWindow/youzhong", "", Mode::right_edge, true},
    {"bottom_right", "PictureViewWindow/youxia", "", Mode::right_edge, true},
    {"bottom_center", "PictureViewWindow/xia", "", Mode::stretch_between_edges, true},
    {"bottom_left", "PictureViewWindow/zuoxia", "", Mode::left_edge, true},
    {"left_center", "PictureViewWindow/zuozhong", "", Mode::left_edge, true},
    {"close_button", "PictureViewWindow/BtnClose", "", Mode::right_edge, true},
});

constexpr auto kOnlineInfoRules = std::to_array<Rule>({
    {"top_left", "olInfo/zuoshang", "", Mode::left_edge, true},
    {"top_center", "olInfo/shang", "", Mode::stretch_between_edges, true},
    {"top_right", "olInfo/youshang", "", Mode::right_edge, true},
    {"right_center", "olInfo/youzhong", "", Mode::right_edge, true},
    {"bottom_right", "olInfo/youxia", "", Mode::right_edge, true},
    {"bottom_center", "olInfo/xia", "", Mode::stretch_between_edges, true},
    {"bottom_left", "olInfo/zuoxia", "", Mode::left_edge, true},
    {"left_center", "olInfo/zuozhong", "", Mode::left_edge, true},
    {"close_button", "olInfo/BtnClose", "", Mode::right_edge, true},
});

// The in-game system menu is assembled from several independent CEGUI roots.
// Keep its dense character/list panels at their authored 4:3 size, and only
// relocate the shared outer chrome plus each page's edge controls.
constexpr auto kInGameToolbarRules = std::to_array<Rule>({
    {"bottom_left_cap", "sysToolBar/di1", "", Mode::left_edge, true},
    {"bottom_connector", "sysToolBar/ditiao", "", Mode::stretch_between_edges, true},
});

constexpr auto kInGameFrameToolbarRules = std::to_array<Rule>({
    {"right_frame", "frameToolbar/Root", "", Mode::right_edge, true},
});

constexpr auto kInGameDecoratorRules = std::to_array<Rule>({
    {"left_border", "decorator/zuo", "", Mode::left_edge, true},
    {"left_leaf_overlay", "decorator/you", "", Mode::left_edge, true},
    {"top_connector", "decorator/shang", "", Mode::stretch_between_edges, true},
});

constexpr auto kInGameInfoRules = std::to_array<Rule>({
    {"right_game_info", "gameInfo/Frame", "", Mode::right_edge, true},
});

constexpr auto kRoleStateRules = std::to_array<Rule>({
    {"left_page_title", "roleStateWindow/duilie", "", Mode::left_edge, true},
});

constexpr auto kPropertyWindowRules = std::to_array<Rule>({
    {"left_page_title", "PropertyWindow/PropertyClassImage", "", Mode::left_edge, true},
    {"right_new_items", "PropertyWindow/NewProp", "", Mode::right_edge, true},
    {"right_healing_items", "PropertyWindow/PropCure", "", Mode::right_edge, true},
    {"right_attack_items", "PropertyWindow/Attack", "", Mode::right_edge, true},
    {"right_support_items", "PropertyWindow/Auxiliary", "", Mode::right_edge, true},
    {"right_materials", "PropertyWindow/PropMaterial", "", Mode::right_edge, true},
    {"right_story_items", "PropertyWindow/PropScenario", "", Mode::right_edge, true},
});

constexpr auto kEquipmentWindowRules = std::to_array<Rule>({
    {"left_page_title", "EquipmentWindow/PropertyClassImage", "", Mode::left_edge, true},
    {"right_equipment_5", "EquipmentWindow/BtnEquipmentClass5", "", Mode::right_edge, true},
    {"right_equipment_0", "EquipmentWindow/BtnEquipmentClass0", "", Mode::right_edge, true},
    {"right_equipment_1", "EquipmentWindow/BtnEquipmentClass1", "", Mode::right_edge, true},
    {"right_equipment_2", "EquipmentWindow/BtnEquipmentClass2", "", Mode::right_edge, true},
    {"right_equipment_3", "EquipmentWindow/BtnEquipmentClass3", "", Mode::right_edge, true},
    {"right_equipment_4", "EquipmentWindow/BtnEquipmentClass4", "", Mode::right_edge, true},
});

constexpr auto kMagicWindowRules = std::to_array<Rule>({
    {"left_page_title", "magicWindow/zuozi", "", Mode::left_edge, true},
    {"right_stunt", "magicWindow/BtnStunt", "", Mode::right_edge, true},
    {"right_water", "magicWindow/BtnMagicWater", "", Mode::right_edge, true},
    {"right_fire", "magicWindow/BtnMagicFire", "", Mode::right_edge, true},
    {"right_thunder", "magicWindow/BtnMagicThunder", "", Mode::right_edge, true},
    {"right_air", "magicWindow/BtnMagicAir", "", Mode::right_edge, true},
    {"right_earth", "magicWindow/BtnMagicEarth", "", Mode::right_edge, true},
});

constexpr auto kSmithWindowRules = std::to_array<Rule>({
    {"left_page_title", "SmithWindow/zuozi", "", Mode::left_edge, true},
    {"right_found", "SmithWindow/ButtonFound", "", Mode::right_edge, true},
    {"right_smithery", "SmithWindow/ButtonSmithery", "", Mode::right_edge, true},
    {"right_enchant", "SmithWindow/ButtonAddMagic", "", Mode::right_edge, true},
});

constexpr auto kMissionWindowRules = std::to_array<Rule>({
    {"left_page_title", "MissionWindow/zj", "", Mode::left_edge, true},
    {"right_missions", "MissionWindow/BtnMission", "", Mode::right_edge, true},
    {"right_story", "MissionWindow/BtnScenario", "", Mode::right_edge, true},
});

constexpr auto kSystemSettingRules = std::to_array<Rule>({
    {"left_page_title", "SystemSetting/cucunyouxi", "", Mode::left_edge, true},
    {"right_button_frame", "SystemSetting/kuang", "", Mode::right_edge, true},
    {"right_save", "SystemSetting/BtnSaveData", "", Mode::right_edge, true},
    {"right_load", "SystemSetting/BtnLoadData", "", Mode::right_edge, true},
    {"right_settings", "SystemSetting/BtnSystemSetting", "", Mode::right_edge, true},
    {"right_exit", "SystemSetting/BtnExit", "", Mode::right_edge, true},
});

// loading.xml keeps its animated progress group as children of the background.
// Once the background is moved to the physical left edge, compensate for that
// parent movement so the original 4:3 progress group remains screen-centred.
constexpr auto kLoadingRules = std::to_array<Rule>({
    {"background", "loading/ditu", "", Mode::stretch_between_edges, true},
    {"progress_light", "loading/blanklightfull", "", Mode::offset_by_padding_factor,
     true, 1.0F},
    {"progress_frame", "loading/blank", "", Mode::offset_by_padding_factor,
     true, 1.0F},
    {"progress_spinner", "loading/ImgPoser", "", Mode::offset_by_padding_factor,
     true, 1.0F},
});

constexpr auto kCombatMainRules = std::to_array<Rule>({
    // The action bar portraits are positioned dynamically by combat code after
    // ProcessInputs. Keep the bar and its portraits together in their original
    // coordinate space instead of moving only the static XML bar.
    {"monster_label", "CombatMainWindow/guai", "", Mode::left_edge, true},
    {"monster_count", "CombatMainWindow/guaishuliang", "", Mode::left_edge, true},
    {"monster_count_mark", "CombatMainWindow/X", "", Mode::left_edge, true},
    {"monster_info", "CombatMainWindow/PanelMonsterInfo", "",
     Mode::offset_by_padding_factor, true, -0.50F},
});

constexpr auto kCombatRoleStateRules = std::to_array<Rule>({
    {"bottom_state_bar", "CombatRoleState/PanelRoleState", "",
     Mode::stretch_between_edges, true},
    {"role_frame_1", "CombatRoleState/ImageFrame1", "",
     Mode::offset_by_padding_factor, true, 0.75F},
    {"role_frame_2", "CombatRoleState/ImageFrame2", "",
     Mode::offset_by_padding_factor, true, 0.75F},
    {"role_frame_3", "CombatRoleState/ImageFrame3", "",
     Mode::offset_by_padding_factor, true, 0.75F},
});

constexpr auto kCombatActionConsoleRules = std::to_array<Rule>({
    {"action_console", "CombatActionConsoleWindow/StaticControlPanel", "",
     Mode::offset_by_padding_factor, true, 0.40F},
});

constexpr auto kCombatMagicSelectRules = std::to_array<Rule>({
    {"magic_select", "CombatMagicSelectWindow/WinSelect", "", Mode::left_edge,
     true, 0.0F, -70.0F},
});

constexpr auto kCombatPropertySelectRules = std::to_array<Rule>({
    {"property_select", "CombatPropertySelectWindow/WinSelect", "", Mode::left_edge,
     true, 0.0F, -70.0F},
});

constexpr auto kCombatStuntSelectRules = std::to_array<Rule>({
    {"stunt_select", "CombatStuntSelectWindow/WinSelect", "", Mode::left_edge,
     true, 0.0F, -70.0F},
});

constexpr auto kCombatGenericSelectRules = std::to_array<Rule>({
    {"generic_select_root", "CombatSelectWindow/Root", "",
     Mode::stretch_between_edges, true},
});

constexpr auto kCombatEndingRules = std::to_array<Rule>({
    {"score_mark", "CombatEndingWindow/PanelScoreMark", "",
     Mode::offset_by_padding_factor, true, -0.20F},
    {"score_grade", "CombatEndingWindow/PanelScoreLv", "", Mode::left_edge, true},
    // PanelRole0..3 begin at the same animation origin and are then expanded
    // into four columns by the original ending animation. Do not capture and
    // overwrite that transient origin from the generic static layout path.
});

constexpr auto kProfiles = std::to_array<WidescreenUiProfile>({
    {"hud_minimap", "minimap/Root", Policy::unchanged,
     kMinimapRules.data(), kMinimapRules.size()},
    {"hud_portrait", "portrait/Root", Policy::unchanged,
     kPortraitRules.data(), kPortraitRules.size()},
    {"title_main", "MainWindow/Root", Policy::remove,
     kMainWindowRules.data(), kMainWindowRules.size()},
    {"title_movie_list", "moviePreviewWindow/Root", Policy::remove,
     kMoviePreviewRules.data(), kMoviePreviewRules.size()},
    {"title_movie_player", "MoviePlayWindow/Root", Policy::remove,
     kMoviePlaybackRules.data(), kMoviePlaybackRules.size()},
    {"title_load", "loadWindow/Root", Policy::remove,
     kLoadWindowRules.data(), kLoadWindowRules.size()},
    {"ingame_save", "SaveWindow/Root", Policy::remove,
     kSaveWindowRules.data(), kSaveWindowRules.size()},
    {"title_cast", "CastWindow/Root", Policy::preserve, nullptr, 0},
    {"title_introduction", "IntroductionWindow/Root", Policy::preserve, nullptr, 0},
    {"title_help", "HelpWindow/Root", Policy::preserve, nullptr, 0},
    {"title_game_info", "gameInfo/Root", Policy::preserve, nullptr, 0},
    {"title_test", "PalTestWindow/Root", Policy::preserve, nullptr, 0},
    {"title_picture_view", "PictureViewWindow/Root", Policy::remove,
     kPictureViewRules.data(), kPictureViewRules.size()},
    {"title_picture_preview", "picturePreviewWindow/Root", Policy::remove,
     kPicturePreviewRules.data(), kPicturePreviewRules.size()},
    {"title_online_info", "olInfo/Root", Policy::remove,
     kOnlineInfoRules.data(), kOnlineInfoRules.size()},
    {"title_repeat_game", "RepeatGameWindow/Root", Policy::preserve, nullptr, 0},
    {"world_map", "WorldMap/Root", Policy::preserve, nullptr, 0},
    {"ingame_toolbar", "sysToolBar/Root", Policy::remove,
     kInGameToolbarRules.data(), kInGameToolbarRules.size()},
    {"ingame_frame_toolbar", "frameToolbar/Root", Policy::unchanged,
     kInGameFrameToolbarRules.data(), kInGameFrameToolbarRules.size()},
    {"ingame_decorator", "decorator/Root", Policy::unchanged,
     kInGameDecoratorRules.data(), kInGameDecoratorRules.size()},
    {"ingame_game_info", "gameInfo/Frame", Policy::unchanged,
     kInGameInfoRules.data(), kInGameInfoRules.size()},
    {"ingame_role", "roleStateWindow/Root", Policy::unchanged,
     kRoleStateRules.data(), kRoleStateRules.size()},
    {"ingame_items", "PropertyWindow/Root", Policy::unchanged,
     kPropertyWindowRules.data(), kPropertyWindowRules.size()},
    {"ingame_equipment", "EquipmentWindow/Root", Policy::unchanged,
     kEquipmentWindowRules.data(), kEquipmentWindowRules.size()},
    {"ingame_magic", "magicWindow/Root", Policy::unchanged,
     kMagicWindowRules.data(), kMagicWindowRules.size()},
    {"ingame_forging", "SmithWindow/Root", Policy::unchanged,
     kSmithWindowRules.data(), kSmithWindowRules.size()},
    {"ingame_missions", "MissionWindow/Root", Policy::unchanged,
     kMissionWindowRules.data(), kMissionWindowRules.size()},
    {"ingame_system", "SystemSetting/Root", Policy::unchanged,
     kSystemSettingRules.data(), kSystemSettingRules.size()},
    {"loading", "loading/Root", Policy::remove,
     kLoadingRules.data(), kLoadingRules.size()},
    {"combat_main", "CombatMainWindow/Root", Policy::unchanged,
     kCombatMainRules.data(), kCombatMainRules.size()},
    {"combat_role_state", "CombatRoleState/Root", Policy::unchanged,
     kCombatRoleStateRules.data(), kCombatRoleStateRules.size()},
    {"combat_action_console", "CombatActionConsoleWindow/StaticControlPanel",
     Policy::unchanged, kCombatActionConsoleRules.data(), kCombatActionConsoleRules.size()},
    {"combat_magic_select", "CombatMagicSelectWindow/Root", Policy::unchanged,
     kCombatMagicSelectRules.data(), kCombatMagicSelectRules.size()},
    {"combat_property_select", "CombatPropertySelectWindow/Root", Policy::unchanged,
     kCombatPropertySelectRules.data(), kCombatPropertySelectRules.size()},
    {"combat_stunt_select", "CombatStuntSelectWindow/Root", Policy::unchanged,
     kCombatStuntSelectRules.data(), kCombatStuntSelectRules.size()},
    {"combat_generic_select", "CombatSelectWindow/Root", Policy::unchanged,
     kCombatGenericSelectRules.data(), kCombatGenericSelectRules.size()},
    {"combat_ending", "CombatEndingWindow/Root", Policy::unchanged,
     kCombatEndingRules.data(), kCombatEndingRules.size()},
});

}  // namespace

bool WidescreenUiWindowNameMatches(
    const std::string_view actual,
    const std::string_view expected_leaf) noexcept {
    if (actual == expected_leaf) {
        return true;
    }
    return actual.size() > expected_leaf.size() &&
        actual[actual.size() - expected_leaf.size() - 1] == '/' &&
        actual.ends_with(expected_leaf);
}

const WidescreenUiProfile* GetWidescreenUiProfiles(std::size_t* const count) noexcept {
    if (count) {
        *count = kProfiles.size();
    }
    return kProfiles.data();
}

const WidescreenUiProfile* FindWidescreenUiProfileByRootName(
    const std::string_view name) noexcept {
    for (const auto& profile : kProfiles) {
        if (WidescreenUiWindowNameMatches(name, profile.trigger_window_name)) {
            return &profile;
        }
    }
    return nullptr;
}

}  // namespace pal4::inject

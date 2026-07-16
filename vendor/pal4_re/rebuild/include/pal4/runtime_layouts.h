#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "pal4/evidence_status.h"

namespace pal4::ida {

inline constexpr std::uint32_t kLaunchExeBase = 0x400000;
inline constexpr std::uint32_t kPal4WinMain = 0x40B700;
inline constexpr std::uint32_t kInitializeGameConfig = 0x408370;
inline constexpr std::uint32_t kCreateGameWindow = 0x409B00;
inline constexpr std::uint32_t kSetViewportCenter = 0x408240;
inline constexpr std::uint32_t kGetRenderSystem = 0x409910;
inline constexpr std::uint32_t kUpdateGameFrame = 0x409280;
inline constexpr std::uint32_t kGameCleanupAndShutdown = 0x5B6030;
inline constexpr std::uint32_t kGameInitializeSystems = 0x5B5EB0;
inline constexpr std::uint32_t kGameInitializeStateAndUi = 0x5B63D0;
inline constexpr std::uint32_t kGameStateGetEntryById = 0x5B6B90;
inline constexpr std::uint32_t kInitPal4GameSystem = 0x5C7E30;
inline constexpr std::uint32_t kGameRenderFrame = 0x5C7F30;
inline constexpr std::uint32_t kGameRenderFrameSceneDrawStage = 0x5C8041;
inline constexpr std::uint32_t kGameRenderFrameOverlayStage = 0x5C80DA;
inline constexpr std::uint32_t kGameRenderFramePresentTailStage = 0x5C812E;
inline constexpr std::uint32_t kGameManagerShutdown = 0x5C83A0;
inline constexpr std::uint32_t kGameManagerCheckAndShutdown = 0x5C83D0;
inline constexpr std::uint32_t kCleanupGameResources = 0x5C8410;
inline constexpr std::uint32_t kPalGameIvGetInstance = 0x5B5AF0;
inline constexpr std::uint32_t kUiFrameManagerGetInstance = 0x4BB650;
inline constexpr std::uint32_t kInitializeCegui = 0x4BC360;
inline constexpr std::uint32_t kRegisterUiWindows = 0x4BD320;
inline constexpr std::uint32_t kPackageOpen = 0x7935F0;
inline constexpr std::uint32_t kPackageOpenFile = 0x7938A0;
inline constexpr std::uint32_t kPackageReadData = 0x793CE0;
inline constexpr std::uint32_t kPackageSeek = 0x793D50;
inline constexpr std::uint32_t kPackageGetFileIndex = 0x793EF0;
inline constexpr std::uint32_t kPackageFindFileIndex = 0x793F40;
inline constexpr std::uint32_t kPackageIndexDecrypt = 0x794260;
inline constexpr std::uint32_t kPkFileManagerInit = 0x66EAF0;
inline constexpr std::uint32_t kPkFileManagerCreate = 0x66EB30;
inline constexpr std::uint32_t kPkFileManagerConstructor = 0x66EBB0;
inline constexpr std::uint32_t kPkFileManagerCleanup = 0x66EE30;
inline constexpr std::uint32_t kPkFileManagerLoadScript = 0x66FED0;

inline constexpr std::ptrdiff_t kGameStateHwndOffset = 0x0;
inline constexpr std::ptrdiff_t kGameStateConfigSourceOffset = 0x4;
inline constexpr std::ptrdiff_t kGameStateFullscreenOffset = 0x8;
inline constexpr std::ptrdiff_t kGameStateWidescreenOffset = 0xC;
inline constexpr std::ptrdiff_t kGameConfigQuitFlagByteOffset = 0xC;
inline constexpr std::ptrdiff_t kGameStateShouldPumpMessagesOffset = 0x20;
inline constexpr std::ptrdiff_t kGameStateWindowReadyOffset = 0x24;
inline constexpr std::ptrdiff_t kGameStateFrameGateOffset = 0x28;
inline constexpr std::ptrdiff_t kGameStateSyncOffset = 0x54C;
inline constexpr std::ptrdiff_t kGameStateFpsCounterOffset = 0x540;
inline constexpr std::ptrdiff_t kGameStateLastFpsCounterOffset = 0x544;
inline constexpr std::ptrdiff_t kGameStateTickNowOffset = 0x56C;
inline constexpr std::ptrdiff_t kGameStateTickReferenceOffset = 0x570;
inline constexpr std::ptrdiff_t kGameStateMinimumFrameSecondsOffset = 0x578;
inline constexpr std::ptrdiff_t kGameStateFixedStepEnabledOffset = 0x580;
inline constexpr std::ptrdiff_t kGameStateCommandLineOverrideOffset = 0x590;
inline constexpr std::ptrdiff_t kPalGameIvStateInitializedFlagIndex = 93;
inline constexpr std::ptrdiff_t kPalGameIvCurrentStateEntryIndex = 99;

struct RenderSystemCallbackEvidence {
    std::ptrdiff_t offset;
    std::uint32_t target_ea;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr std::array<RenderSystemCallbackEvidence, 11> kRenderSystemCallbackEvidence = {{
    {48, kGameManagerShutdown, EvidenceStatus::verified_in_ida, "GameManager_Shutdown slot"},
    {52, kGameManagerShutdown, EvidenceStatus::verified_in_ida, "GameManager_Shutdown slot"},
    {56, kGameManagerCheckAndShutdown, EvidenceStatus::verified_in_ida, "GameManager_CheckAndShutdown slot"},
    {60, kGameManagerShutdown, EvidenceStatus::verified_in_ida, "GameManager_Shutdown slot"},
    {64, kGameManagerShutdown, EvidenceStatus::verified_in_ida, "GameManager_Shutdown slot"},
    {68, kGameManagerShutdown, EvidenceStatus::verified_in_ida, "GameManager_Shutdown slot"},
    {72, kGameManagerShutdown, EvidenceStatus::verified_in_ida, "GameManager_Shutdown slot"},
    {84, kGameManagerShutdown, EvidenceStatus::verified_in_ida, "GameManager_Shutdown slot"},
    {88, kGameManagerShutdown, EvidenceStatus::verified_in_ida, "GameManager_Shutdown slot"},
    {92, kGameManagerShutdown, EvidenceStatus::verified_in_ida, "GameManager_Shutdown slot"},
    {96, kCleanupGameResources, EvidenceStatus::verified_in_ida, "Cleanup_GameResources slot"},
}};

struct FrameDispatchEvidence {
    std::uint32_t current_frame_callback_ea;
    EvidenceStatus callback_status;
    std::ptrdiff_t current_state_entry_index;
    std::uint32_t state_entry_lookup_ea;
    EvidenceStatus state_entry_status;
};

inline constexpr FrameDispatchEvidence kFrameDispatchEvidence{
    kGameRenderFrame,
    EvidenceStatus::verified_in_ida,
    kPalGameIvCurrentStateEntryIndex,
    kGameStateGetEntryById,
    EvidenceStatus::verified_in_ida,
};

struct ThreadBootstrapEvidence {
    std::string_view name;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr std::array<ThreadBootstrapEvidence, 5> kThreadBootstrapEvidence = {{
    {"thread_count_seed", EvidenceStatus::verified_in_ida, "CRT_Rand()%8 then +1 stored to g_ThreadCount"},
    {"worker_threads_spawned", EvidenceStatus::verified_in_ida, "CreateThread(StartAddress) loop decrements g_ThreadCount to zero"},
    {"parameter_thread_spawned", EvidenceStatus::verified_in_ida, "secondary CreateThread uses &Parameter"},
    {"thread_ready_wait", EvidenceStatus::verified_in_ida, "while (!g_ThreadCount) Sleep(1000)"},
    {"randomized_memory_layout", EvidenceStatus::verified_in_ida, "AllocateMemory(v12 + v14 + 1604), then random offsets for g_GameConfig/g_GameState"},
}};

struct GlobalOwnershipEvidence {
    std::string_view name;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr std::array<GlobalOwnershipEvidence, 4> kGlobalOwnershipEvidence = {{
    {"config_state_shared_allocation", EvidenceStatus::verified_in_ida, "single AllocateMemory result hosts both g_GameConfig and g_GameState with random offsets"},
    {"render_system_role", EvidenceStatus::verified_in_ida, "GetRenderSystem supplies callback-bearing host used by CreateGameWindow and init_pal4_game_system"},
    {"palgameiv_role", EvidenceStatus::verified_in_ida, "PALGameIV singleton owns current state entry slot and shutdown path participation"},
    {"uiframemanager_role", EvidenceStatus::verified_in_ida, "UIFrameManager participates in state/UI init and current-state cursor management"},
}};

struct InitSystemsStageEvidence {
    std::string_view name;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr std::array<InitSystemsStageEvidence, 5> kCpkInfrastructureEvidence = {{
    {"pkfilemanager_singleton_registration", EvidenceStatus::verified_in_ida, "PKFileManager_Init registers pkFileMgr singleton via SCL_SingletonManager"},
    {"package_header_and_index_load", EvidenceStatus::verified_in_ida, "Package_Open reads 0x80-byte RST header, then loads and decrypts 0x20-byte entry records"},
    {"package_lookup_by_hash", EvidenceStatus::verified_in_ida, "Package_GetFileIndex lowercases the path, computes PAL4 hash, then Package_FindFileIndex binary-searches entries"},
    {"package_openfile_read_seek", EvidenceStatus::verified_in_ida, "Package_OpenFile prepares a file handle; Package_ReadData and Package_Seek read/seek through compressed or raw file payload"},
    {"pkfilemanager_script_mounts", EvidenceStatus::verified_in_ida, "PKFileManager_LoadAllScripts mounts ui/database/effect/matfx/2d and related gamedata packages"},
}};

inline constexpr std::array<InitSystemsStageEvidence, 5> kInitSystemsStageEvidence = {{
    {"preload_math_and_scripts", EvidenceStatus::verified_in_ida, "math tables, optional script preload, lights, loading frame"},
    {"database_inventory_script_camera_assets", EvidenceStatus::verified_in_ida, "database, inventory, script object, camera, texture dict, fonts"},
    {"effects_clump_actor_player", EvidenceStatus::verified_in_ida, "effect bootstrap, clump, actor message, player manager, actor control"},
    {"ui_combat_audio", EvidenceStatus::verified_in_ida, "InitializeCEGUI, combat init, audio init"},
    {"game_objects_and_ui_registration", EvidenceStatus::verified_in_ida, "game components, object registration, core systems, dialog settings, RegisterUIWindows, console commands"},
}};

inline constexpr std::array<InitSystemsStageEvidence, 4> kStateUiStageEvidence = {{
    {"state_label_registration", EvidenceStatus::verified_in_ida, "one-time label/message registration and atexit hook"},
    {"state_entry_lookup_and_leave", EvidenceStatus::verified_in_ida, "lookup target entry, leave previous state if present"},
    {"ui_cursor_and_entry_call", EvidenceStatus::verified_in_ida, "reset UI cursor and invoke target entry virtual call"},
    {"timebase_reset_and_activate", EvidenceStatus::verified_in_ida, "store current state pointer and reset g_GameState timing fields"},
}};

struct ViewportCenterEvidence {
    std::uint32_t function_ea;
    EvidenceStatus status;
    std::ptrdiff_t requires_pump_messages_offset;
    std::ptrdiff_t resets_state_offset;
    std::ptrdiff_t hwnd_offset;
    std::string_view note;
};

inline constexpr ViewportCenterEvidence kViewportCenterEvidence{
    kSetViewportCenter,
    EvidenceStatus::verified_in_ida,
    kGameStateShouldPumpMessagesOffset,
    0x2C,
    kGameStateHwndOffset,
    "uses width/2 and height/2, clears g_GameState+44, then ClientToScreen/SetCursorPos when pump-messages gate is enabled",
};

struct PresentationStageEvidence {
    std::string_view name;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr std::array<PresentationStageEvidence, 3> kPresentationStageEvidence = {{
    {"show_window", EvidenceStatus::verified_in_ida, "ShowWindow(*(HWND*)g_GameState, nShowCmd) after init_pal4_game_system"},
    {"update_window", EvidenceStatus::verified_in_ida, "UpdateWindow(*(HWND*)g_GameState) immediately after ShowWindow"},
    {"viewport_center", EvidenceStatus::verified_in_ida, "SetViewportCenter(width/2, height/2) before entering main loop"},
}};

struct FirstFrameEvidence {
    std::uint32_t frame_callback_ea;
    std::ptrdiff_t should_pump_messages_offset;
    std::ptrdiff_t window_ready_offset;
    std::ptrdiff_t frame_gate_offset;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr FirstFrameEvidence kFirstFrameEvidence{
    kGameRenderFrame,
    kGameStateShouldPumpMessagesOffset,
    kGameStateWindowReadyOffset,
    kGameStateFrameGateOffset,
    EvidenceStatus::verified_in_ida,
    "first-frame gate opens only after viewport-centering and state-entry setup; update_game_frame runs behind g_GameState+0x20/+0x24 with one-time seeding at +0x28",
};

inline constexpr std::array<InitSystemsStageEvidence, 5> kRenderFrameStageEvidence = {{
    {"video_mode_gate", EvidenceStatus::verified_in_ida, "HandleVideoModeChange gates the whole frame"},
    {"camera_audio_update", EvidenceStatus::verified_in_ida, "camera object lookup, state callback, and AudioSystem_UpdateAllSounds"},
    {"camera_begin_and_scene_draw", EvidenceStatus::verified_in_ida, "RwCameraClear/BeginUpdate and scene draw callback"},
    {"overlay_and_present", EvidenceStatus::verified_in_ida, "optional overlays, RwCameraEndUpdate, ShowGameRaster, render-system cleanup"},
    {"device_loss_tail", EvidenceStatus::verified_in_ida, "HandleDeviceLoss completes the frame path"},
}};

struct RenderFrameDynamicBreakpointEvidence {
    std::uint32_t address;
    std::string_view name;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr std::array<RenderFrameDynamicBreakpointEvidence, 3> kRenderFrameDynamicBreakpointEvidence = {{
    {kGameRenderFrameSceneDrawStage, "scene_draw_stage", EvidenceStatus::verified_in_ida, "render-state setup immediately before scene draw callback inside Game_RenderFrame"},
    {kGameRenderFrameOverlayStage, "overlay_stage", EvidenceStatus::verified_in_ida, "overlay/text rendering branch after optional textured rectangle path"},
    {kGameRenderFramePresentTailStage, "present_tail_stage", EvidenceStatus::verified_in_ida, "post-present texture/property tail before HandleDeviceLoss"},
}};

inline constexpr std::array<InitSystemsStageEvidence, 4> kShutdownStageEvidence = {{
    {"game_manager_shutdown", EvidenceStatus::verified_in_ida, "Game_CleanupAndShutdown(PALGameIV_GetInstance())"},
    {"render_system_cleanup", EvidenceStatus::verified_in_ida, "cleanup_render_system(GetRenderSystem())"},
    {"memory_exit_finalize", EvidenceStatus::verified_in_ida, "finalize_game_exit() releases lpMem chain via HeapFree"},
    {"window_not_ready", EvidenceStatus::verified_in_ida, "window readiness is dropped after shutdown path completes"},
}};

inline constexpr std::array<InitSystemsStageEvidence, 5> kStateLifecycleStageEvidence = {{
    {"state_collection_lookup", EvidenceStatus::verified_in_ida, "Game_State_GetEntryById walks PALGameIV state list from this[98]"},
    {"previous_state_leave", EvidenceStatus::verified_in_ida, "if PALGameIV[99] exists, virtual leave callback runs before transition"},
    {"target_state_enter", EvidenceStatus::verified_in_ida, "target entry virtual call at +4 must succeed before PALGameIV[99] is replaced"},
    {"current_state_activate", EvidenceStatus::verified_in_ida, "PALGameIV[99] is updated to target state on success"},
    {"state_shutdown_gate", EvidenceStatus::verified_in_ida, "GameManager_CheckAndShutdown / GameManager_Shutdown observe PALGameIV+396 state object status"},
}};

struct StateTokenEvidence {
    std::string_view paliv_label;
    int token;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr int kStateTokenUnknown = 13;
inline constexpr std::array<StateTokenEvidence, 13> kStateTokenEvidenceTable = {{
    {"PALIV_LOGO", 0, EvidenceStatus::verified_in_ida, "Game_InitializeStateAndUI@0x5B63D0 uses arg_0 as token index (arg_0*0x10 from byte_8FDE08), and registration slot 0 is PALIV_LOGO; caller init_pal4_game_system@0x5C7F2C passes 0"},
    {"PALIV_SCENE", 1, EvidenceStatus::verified_in_ida, "Game_InitializeStateAndUI@0x5B63D0 registration slot 1 is PALIV_SCENE; callers at 0x58F85C/0x58F8AA/0x58FEDB/0x58FFFD pass 1"},
    {"PALIV_SYSTEM", 2, EvidenceStatus::verified_in_ida, "Game_InitializeStateAndUI@0x5B63D0 registration slot 2 is PALIV_SYSTEM; caller sub_479070@0x47907B passes 2"},
    {"PALIV_COMBAT", 3, EvidenceStatus::verified_in_ida, "Game_InitializeStateAndUI@0x5B63D0 registration slot 3 is PALIV_COMBAT and arg_0 is consumed as direct slot index"},
    {"PALIV_TRADE", 4, EvidenceStatus::verified_in_ida, "Game_InitializeStateAndUI@0x5B63D0 registration slot 4 is PALIV_TRADE and arg_0 is consumed as direct slot index"},
    {"PALIV_FOUNDRY", 5, EvidenceStatus::verified_in_ida, "Game_InitializeStateAndUI@0x5B63D0 registration slot 5 is PALIV_FOUNDRY and arg_0 is consumed as direct slot index"},
    {"PALIV_LOADING", 6, EvidenceStatus::verified_in_ida, "Game_InitializeStateAndUI@0x5B63D0 registration slot 6 is PALIV_LOADING and arg_0 is consumed as direct slot index"},
    {"PALIV_GAME_OVER", 7, EvidenceStatus::verified_in_ida, "Game_InitializeStateAndUI@0x5B63D0 registration slot 7 is PALIV_GAME_OVER and arg_0 is consumed as direct slot index"},
    {"PALIV_MINIGAME", 8, EvidenceStatus::verified_in_ida, "Game_InitializeStateAndUI@0x5B63D0 registration slot 8 is PALIV_MINIGAME and arg_0 is consumed as direct slot index"},
    {"PALIV_GAME_SAVE", 9, EvidenceStatus::verified_in_ida, "Game_InitializeStateAndUI@0x5B63D0 registration slot 9 is PALIV_GAME_SAVE; caller GameState_Transition_Conditional@0x59C785 passes 9"},
    {"PALIV_MODAL_FRAME", 10, EvidenceStatus::verified_in_ida, "Game_InitializeStateAndUI@0x5B63D0 registration slot 10 is PALIV_MODAL_FRAME and arg_0 is consumed as direct slot index"},
    {"PALIV_WORLD_MAP", 11, EvidenceStatus::verified_in_ida, "Game_InitializeStateAndUI@0x5B63D0 registration slot 11 is PALIV_WORLD_MAP and arg_0 is consumed as direct slot index"},
    {"PALIV_BINK", 12, EvidenceStatus::verified_in_ida, "Game_InitializeStateAndUI@0x5B63D0 registration slot 12 is PALIV_BINK and arg_0 is consumed as direct slot index"},
}};

struct StateTransitionEvidence {
    std::string_view transition_symbol;
    std::string_view transition_label;
    std::string_view target_label;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr std::array<StateTransitionEvidence, 1> kStateTransitionEvidence = {{
    {"PAL4-NewGame", "[NewGame]", "PALIV_SCENE", EvidenceStatus::verified_in_ida, "PALGameIV_Initialize@0x5B61E0 constructs transition symbol/label strings from PAL4-NewGame and [NewGame]; scene startup path enters state token 1 (PALIV_SCENE) via Game_InitializeStateAndUI"},
}};

struct StateEnterArgEvidence {
    std::uint32_t caller_ea;
    int target_token;
    bool arg2_nonzero;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr std::array<StateEnterArgEvidence, 44> kStateEnterArgEvidence = {{
    {0x5C7F2C, 0, false, EvidenceStatus::verified_in_ida, "init_pal4_game_system passes Game_InitializeStateAndUI(..., 0, 0)"},
    {0x47907B, 2, false, EvidenceStatus::verified_in_ida, "GameManager_SetStateHelper passes (..., 2, 0)"},
    {0x5D6D6C, 2, false, EvidenceStatus::verified_in_ida, "UI_OpenMenuByIndex helper path calls Game_InitializeStateAndUI(..., 2, 0)"},
    {0x58F85C, 1, false, EvidenceStatus::verified_in_ida, "GameState_TransitionAndLoad path #1 passes (..., 1, 0)"},
    {0x58F8AA, 1, false, EvidenceStatus::verified_in_ida, "GameState_TransitionAndLoad path #2 passes (..., 1, 0)"},
    {0x4350F0, 3, true, EvidenceStatus::verified_in_ida, "GameManager_InitializeCombatSystem passes XMLDocument pointer as arg2"},
    {0x448251, 6, true, EvidenceStatus::verified_in_ida, "UIObject_HandleSaveLoad passes XMLDocument pointer as arg2"},
    {0x58B112, 1, true, EvidenceStatus::verified_in_ida, "scriptGameSceneManagerUpdate passes configStruct pointer as arg2"},
    {0x58B218, 1, true, EvidenceStatus::verified_in_ida, "scriptGameSceneManagerUpdate alternate branch passes v21 XMLDocument pointer as arg2"},
    {0x5D7C39, 10, true, EvidenceStatus::verified_in_ida, "Game_ShowLoadScreen passes XMLDocument pointer as arg2"},
    {0x5E7C93, 10, true, EvidenceStatus::verified_in_ida, "Dialog_ShowSelect path passes XMLDocument pointer as arg2"},
    {0x5E8134, 11, true, EvidenceStatus::verified_in_ida, "WorldMap_Show path passes XMLDocument pointer as arg2"},
    {0x5E6822, 12, true, EvidenceStatus::verified_in_ida, "Movie_Play path passes XMLDocument pointer as arg2"},
    {0x5E8F43, 12, true, EvidenceStatus::verified_in_ida, "Movie_Play_2 path #1 passes XMLDocument pointer as arg2"},
    {0x5E8F56, 12, true, EvidenceStatus::verified_in_ida, "Movie_Play_2 path #2 repeats Game_InitializeStateAndUI(..., 12, XMLDocument*)"},
    {0x5D3DBB, 5, false, EvidenceStatus::verified_in_ida, "GameLogic_HandlePostEventState branch passes (..., 5, 0)"},
    {0x5D45DC, 3, true, EvidenceStatus::verified_in_ida, "Combat_Start path passes XMLDocument_InitFromString result as arg2"},
    {0x58AD29, 1, true, EvidenceStatus::verified_in_ida, "script_ExecuteScriptAndManageGame passes XMLDocument pointer as arg2"},
    {0x58B4C9, 0, false, EvidenceStatus::verified_in_ida, "script_UpdateGameTimer timeout path calls Game_InitializeStateAndUI(..., 0, 0)"},
    {0x48167A, 0, false, EvidenceStatus::verified_in_ida, "quitGame_confirmed path calls Game_InitializeStateAndUI(..., 0, 0)"},
    {0x48346F, 1, false, EvidenceStatus::verified_in_ida, "SystemToolbarUI_Close fallback path calls Game_InitializeStateAndUI(..., 1, 0)"},
    {0x5E68D6, 0, false, EvidenceStatus::verified_in_ida, "Movie_Update completion branch calls Game_InitializeStateAndUI(..., 0, 0)"},
    {0x5E6963, 0, false, EvidenceStatus::verified_in_ida, "Movie_Stop branch calls Game_InitializeStateAndUI(..., 0, 0)"},
    {0x5E8C32, 0, false, EvidenceStatus::verified_in_ida, "scriptGameInitLogHandler calls Game_InitializeStateAndUI(..., 0, 0) before handler setup"},
    {0x5D55A0, 0, false, EvidenceStatus::verified_in_ida, "Game_ReturnToWorld helper calls Game_InitializeStateAndUI(..., 0, 0)"},
    {0x59C785, 9, false, EvidenceStatus::verified_in_ida, "GameState_Transition_Conditional calls Game_InitializeStateAndUI(..., 9, 0)"},
    {0x4791BB, 10, true, EvidenceStatus::verified_in_ida, "UI_HelpButton_ClickHandler path passes XMLDocument pointer as arg2"},
    {0x49FEA3, 1, true, EvidenceStatus::verified_in_ida, "ui_SetWindowVisibleAlt path passes XMLDocument pointer as arg2"},
    {0x4A114D, 1, true, EvidenceStatus::verified_in_ida, "ui_SetWindowVisible path passes XMLDocument pointer as arg2"},
    {0x50FC14, 4, false, EvidenceStatus::verified_in_ida, "ui_SetTradeItemInfo starts state 4 with arg2=0"},
    {0x50FCB4, 4, false, EvidenceStatus::verified_in_ida, "ui_ClearTradeItemInfo starts state 4 with arg2=0"},
    {0x50FCFE, 1, false, EvidenceStatus::verified_in_ida, "ui_SetTradeState transitions to state 1 with arg2=0"},
    {0x5E7086, 8, true, EvidenceStatus::verified_in_ida, "PuzzleGame_Start path passes XMLDocument pointer as arg2"},
    {0x5E725A, 8, true, EvidenceStatus::verified_in_ida, "PuzzleGame_Start_2 path passes XMLDocument pointer as arg2"},
    {0x4AEF39, 10, true, EvidenceStatus::verified_in_ida, "eventHandler_ProcessEvent timer-frame branch passes XMLDocument pointer as arg2"},
    {0x5BB892, 6, true, EvidenceStatus::verified_in_ida, "script_load_and_process_game_data builds config XML and passes it as arg2"},
    {0x5BCB48, 8, true, EvidenceStatus::verified_in_ida, "start_puzzle console path passes XMLDocument pointer as arg2"},
    {0x5BCDAC, 8, true, EvidenceStatus::verified_in_ida, "start_jigsaw console path passes XMLDocument pointer as arg2"},
    {0x58FFFD, 1, false, EvidenceStatus::verified_in_ida, "UI_CheckVisibilityAndTransition calls Game_InitializeStateAndUI(..., 1, 0)"},
    {0x58FEDB, 1, false, EvidenceStatus::verified_in_ida, "Dialog_CheckVisibilityAndTransitionGameState calls (..., 1, 0)"},
    {0x481949, 6, true, EvidenceStatus::verified_in_ida, "saveList_onDoubleClick path passes XMLDocument pointer as arg2"},
    {0x486822, 5, true, EvidenceStatus::verified_in_ida, "shopUI_SearchDialogText path passes XMLDocument pointer as arg2"},
    {0x5D3287, 6, true, EvidenceStatus::verified_in_ida, "Game_InitializeAndLoadConfig path builds config XML and passes it as arg2"},
    {0x5D8105, 10, true, EvidenceStatus::verified_in_ida, "UI_ShowYesNoDialog path passes XMLDocument pointer as arg2"},
}};

inline constexpr std::size_t kGameInitializeStateAndUiXrefCountObserved = 44;
inline constexpr std::size_t kStateEnterArgEvidenceCount = kStateEnterArgEvidence.size();
inline constexpr bool kStateEnterArgEvidenceCoverageComplete =
    kStateEnterArgEvidenceCount == kGameInitializeStateAndUiXrefCountObserved;

struct TransitionEnterArgEvidence {
    std::string_view transition_symbol;
    int arg2_value;
    bool arg2_nonzero;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr std::array<TransitionEnterArgEvidence, 1> kTransitionEnterArgEvidence = {{
    {"PAL4-NewGame", 0, false, EvidenceStatus::verified_in_ida, "NewGame transition path reaches Game_InitializeStateAndUI(..., 1, 0) at 0x58F85C/0x58F8AA"},
}};

struct StateCatalogEvidence {
    std::string_view paliv_label;
    std::uint32_t entry_ea;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr std::array<StateCatalogEvidence, 6> kStateCatalogEvidence = {{
    {"PALIV_LOGO", 0x58FCA0, EvidenceStatus::partially_verified, "verified label registration; leave entry confirmed"},
    {"PALIV_SCENE", 0x58F0B0, EvidenceStatus::partially_verified, "verified label registration; world/scene entry confirmed"},
    {"PALIV_SYSTEM", 0x590340, EvidenceStatus::partially_verified, "verified label registration; system entry confirmed"},
    {"PALIV_COMBAT", 0x58B2D0, EvidenceStatus::partially_verified, "verified label registration; combat leave confirmed"},
    {"PALIV_TRADE", 0x590570, EvidenceStatus::partially_verified, "verified label registration; trade entry confirmed"},
    {"PALIV_FOUNDRY", 0x590190, EvidenceStatus::partially_verified, "verified label registration; smith/foundry entry confirmed"},
}};

inline constexpr std::array<InitSystemsStageEvidence, 4> kLogoPathBridgeEvidence = {{
    {"logo_leave_wrapper", EvidenceStatus::verified_in_ida, "GameLogo_Leave_Wrapper dispatches to pal4_PALLogoEntry::SFLB_Leave_GameLogo"},
    {"logo_ui_event_queue", EvidenceStatus::verified_in_ida, "logo leave path pushes a zeroed pair through ProcessUIEvents(UIFrameManager+260, g_UIEventQueue, ...)"},
    {"logo_cegui_toggle", EvidenceStatus::verified_in_ida, "logo leave path toggles CEGUI windows off via UIFrameManager"},
    {"logo_effect_cleanup", EvidenceStatus::verified_in_ida, "logo leave path triggers effect cleanup through UIFrameManager/effect system"},
}};

inline constexpr std::array<InitSystemsStageEvidence, 5> kMainMenuUiBridgeEvidence = {{
    {"cegui_initialized", EvidenceStatus::verified_in_ida, "InitializeCEGUI(UIFrameManager_GetInstance()) is part of Game_InitializeSystems"},
    {"ui_event_queue_dispatch", EvidenceStatus::verified_in_ida, "main menu handlers dispatch through ProcessUIEvents(UIFrameManager+260, ...)"},
    {"new_game_handler_known", EvidenceStatus::verified_in_ida, "StartNewGameButtonHandler at 0x448930"},
    {"load_game_handler_known", EvidenceStatus::verified_in_ida, "UI_LoadGameButtonHandler at 0x448AC0"},
    {"exit_game_handler_known", EvidenceStatus::verified_in_ida, "UI_ProcessExitGameEvent at 0x448A80"},
}};

inline constexpr std::array<InitSystemsStageEvidence, 10> kInitializeCeguiStageEvidence = {{
    {"ui_manager_entry", EvidenceStatus::verified_in_ida, "UIFrameManager_GetInstance result is passed into InitializeCEGUI(0x4BC360) from Game_InitializeSystems before RegisterUIWindows"},
    {"renderer_select_and_construct", EvidenceStatus::verified_in_ida, "resolution-dependent renderer object is allocated and constructed before CEGUI_System_Initialize"},
    {"system_initialize", EvidenceStatus::verified_in_ida, "CEGUI_System_Initialize writes the system object to this+408"},
    {"set_uv_alignment", EvidenceStatus::verified_in_ida, "CEGUI::System::setUVAlignment runs immediately after successful system init"},
    {"frame_manager_bootstrap", EvidenceStatus::verified_in_ida, "InitializeUIFrameManager and uiFrameManager_Initialize run before desktop creation"},
    {"default_font_and_cursor", EvidenceStatus::verified_in_ida, "default font 'system' is set and MouseCursor visibility byte is cleared"},
    {"desktop_creation", EvidenceStatus::verified_in_ida, "WindowManager singleton creates DefaultWindow named Desktop and sets its size"},
    {"desktop_event_bindings_and_sheet", EvidenceStatus::verified_in_ida, "Desktop binds key up/down handlers, then System::setGUISheet installs it"},
    {"oiramlook_imageset_binding", EvidenceStatus::verified_in_ida, "ImagesetManager loads OIRAMLOOK and StaticImage::setImage binds images to windows"},
    {"post_ui_support_init", EvidenceStatus::partially_verified, "numeric spinner pool, effect enable, and rich-text strings continue after the main-menu-relevant prefix"},
}};

inline constexpr std::array<InitSystemsStageEvidence, 3> kUiInitializationChainEvidence = {{
    {"initialize_cegui_before_register_ui_windows", EvidenceStatus::verified_in_ida, "Game_InitializeSystems calls InitializeCEGUI(0x4BC360) before RegisterUIWindows(0x4BD320)"},
    {"initialize_ui_frame_manager_inside_initialize_cegui", EvidenceStatus::verified_in_ida, "InitializeCEGUI reaches InitializeUIFrameManager / uiFrameManager_Initialize before desktop creation"},
    {"rebuilt_preload_before_coarse_initialize_cegui", EvidenceStatus::partially_verified, "current rebuilt bootstrap preloads UI resources/layouts for observability before it emits the coarse InitializeCEGUI stage log"},
}};

struct CeguiRendererVariantEvidence {
    int width;
    int height;
    std::uint32_t renderer_ctor_ea;
    std::uint32_t renderer_size;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr std::array<CeguiRendererVariantEvidence, 7> kInitializeCeguiRendererVariants = {{
    {1280, 800, 0x413670, 0x12C, EvidenceStatus::verified_in_ida, "special renderer ctor for 1280x800"},
    {1280, 960, 0x413620, 0x120, EvidenceStatus::verified_in_ida, "special renderer ctor for 1280x960"},
    {1152, 864, 0x4135D0, 0x120, EvidenceStatus::verified_in_ida, "special renderer ctor for 1152x864"},
    {1024, 768, 0x413580, 0x118, EvidenceStatus::verified_in_ida, "special renderer ctor for 1024x768"},
    {1680, 1050, 0x413580, 0x118, EvidenceStatus::verified_in_ida, "shared renderer ctor for 1680x1050"},
    {1920, 1080, 0x413580, 0x118, EvidenceStatus::verified_in_ida, "shared renderer ctor for 1920x1080"},
    {1600, 900, 0x413580, 0x118, EvidenceStatus::verified_in_ida, "shared renderer ctor for 1600x900"},
}};

struct CeguiSystemInitEvidence {
    std::uint32_t function_ea;
    std::uint32_t system_allocation_size;
    std::uint32_t logger_zero_offset;
    std::uint32_t imported_get_singleton_ea;
    std::uint32_t imported_get_singleton_ptr_ea;
    std::uint32_t imported_set_uv_alignment_ea;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr CeguiSystemInitEvidence kCeguiSystemInitEvidence{
    0x410450,
    0x1B0,
    0x0,
    0x8412A0,
    0x8412A8,
    0x84138C,
    EvidenceStatus::verified_in_ida,
    "allocates CEGUI::System(0x1B0), calls external System ctor with renderer/resource-provider variant, clears Logger singleton head, derives scale factors from g_GameConfig, then EXE-side InitializeCEGUI immediately consumes imported getSingletonPtr/setUVAlignment without an obvious singleton-publish import",
};

inline constexpr std::array<std::string_view, 11> kRegisteredFrameNames = {{
    "roleStateWindow",
    "EquipmentWindow",
    "magicWindow",
    "MissionWindow",
    "PropertyWindow",
    "SystemSetting",
    "sysToolBar",
    "SmithWindow",
    "TradeWindow",
    "sceneWindow",
    "portrait",
}};

inline constexpr std::array<std::string_view, 4> kUiCursorLoadOrderNames = {{
    "default.cur",
    "none.ico",
    "pickup.ani",
    "talk.ani",
}};

inline constexpr std::array<std::string_view, 4> kUiCursorSelectionTable = {{
    "default.cur",  // off_8B1C70[0]
    "talk.ani",     // off_8B1C70[1]
    "pickup.ani",   // off_8B1C70[2]
    "none.ico",     // off_8B1C70[3]
}};

struct WinMainGateEvidence {
    std::ptrdiff_t offset;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr std::array<WinMainGateEvidence, 4> kWinMainGateEvidence = {{
    {kGameConfigQuitFlagByteOffset, EvidenceStatus::verified_in_ida, "while (!*((BYTE*)g_GameConfig + 12))"},
    {kGameStateShouldPumpMessagesOffset, EvidenceStatus::verified_in_ida, "if (*(g_GameState + 32)) else WaitMessage"},
    {kGameStateWindowReadyOffset, EvidenceStatus::verified_in_ida, "if (*(g_GameState + 36)) update_game_frame"},
    {kGameStateFrameGateOffset, EvidenceStatus::verified_in_ida, "if (!*(g_GameState + 40)) set and wait parameter"},
}};

struct ClassEvidence {
    std::string_view name;
    std::uint32_t evidence_address;
    std::uint32_t x86_size;
    EvidenceStatus status;
    std::string_view detail;
};

inline constexpr std::array<ClassEvidence, 4> kClassEvidence = {{
    {"PALGameIV", kPalGameIvGetInstance, 444, EvidenceStatus::partially_verified, "verified singleton pattern, verified x86 size"},
    {"uiFrameManager", kUiFrameManagerGetInstance, 548, EvidenceStatus::partially_verified, "verified singleton pattern, verified member offset +260"},
    {"ScriptInterpreter", 0x7DE4C0, 28, EvidenceStatus::partially_verified, "factory verified, layout incomplete"},
    {"CSBInterpreter", 0x7E0E80, 40, EvidenceStatus::partially_verified, "factory verified, csb context partially mapped"},
}};

}  // namespace pal4::ida

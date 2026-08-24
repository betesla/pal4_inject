#pragma once

#include <cstddef>
#include <cstdint>

#include "pal4/runtime_layouts.h"

namespace pal4::inject::ida {

inline constexpr std::uint32_t kLaunchExeBase = pal4::ida::kLaunchExeBase;

inline constexpr std::uint32_t kProcessUiEvent = 0x411900;
inline constexpr std::uint32_t kHandleUiMessageAndProcess = 0x4BF0E0;
inline constexpr std::uint32_t kUiFrameManagerSetCursor = 0x4BBB70;
inline constexpr std::uint32_t kSimulateKeyPressAndRelease = 0x4BFD70;
inline constexpr std::uint32_t kGetInputManager = 0x408060;
inline constexpr std::uint32_t kInputManagerGetKeyState = 0x4077E0;
inline constexpr std::uint32_t kProcessInputs = 0x4080B0;
inline constexpr std::uint32_t kUpdateInputDeviceState = 0x407910;
inline constexpr std::uint32_t kInitializeDirectInput = 0x407640;
inline constexpr std::uint32_t kUpdateKeyOrButtonState = 0x407A00;
inline constexpr std::uint32_t kUpdateKeyTimingInfo = 0x407FA0;
inline constexpr std::uint32_t kGiTalkScriptCallback = 0x5DFB10;
inline constexpr std::uint32_t kCeguiRendererConstructor2 = 0x413580;
inline constexpr std::uint32_t kCeguiSystemInitialize = 0x410450;
inline constexpr std::uint32_t kLoadFontFile = 0x4BD3B0;
inline constexpr std::uint32_t kSetupMinimapTexture = 0x40DE10;
inline constexpr std::uint32_t kSetProperties4C2550 = 0x4C2550;
inline constexpr std::uint32_t kCombatConsoleSetImageAndPosition = kSetProperties4C2550;
inline constexpr std::uint32_t kRenderTextAndImage = 0x4C26A0;
inline constexpr std::uint32_t kUiShowCombatHint = 0x54A1F0;
inline constexpr std::uint32_t kCombatConsoleSetImageAndPosition2 = kUiShowCombatHint;
inline constexpr std::uint32_t kUiShowCombatHint2 = 0x54A960;
inline constexpr std::uint32_t kUiShowCombatResult = kUiShowCombatHint2;
inline constexpr std::uint32_t kCameraPrepare = 0x5E4660;
inline constexpr std::uint32_t kCameraRunSingle = 0x5E46B0;
inline constexpr std::uint32_t kCameraUpdateMatrix = 0x5EA190;
inline constexpr std::uint32_t kD3d9SetPresentParameters = 0x75F710;
inline constexpr std::uint32_t kPal4MainWndProc = 0x40A170;
inline constexpr std::uint32_t kHandlePlayerInputEvents = 0x4283B0;
inline constexpr std::uint32_t kPlayerControlUpdate = 0x427660;
inline constexpr std::uint32_t kMovePlayerInDirection = 0x427E70;
inline constexpr std::uint32_t kSetPlayerMovementMode = 0x428190;
inline constexpr std::uint32_t kSetCameraModeScript = 0x5BC650;
inline constexpr std::uint32_t kAudioSystemPlayMusic = 0x658E90;
inline constexpr std::uint32_t kGiPlayMovieScriptCallback = 0x5E66A0;
inline constexpr std::uint32_t kBinkPlayerOpenVideo = 0x65C810;
inline constexpr std::uint32_t kBinkPlayerUpdateAndRender = 0x65D170;
inline constexpr std::uint32_t kBinkUpdateVideo = 0x65CA40;
inline constexpr std::uint32_t kBinkPlayerCloseVideo = 0x65D0A0;
inline constexpr std::uint32_t kDrawTexturedRectangle = 0x419A60;
inline constexpr std::uint32_t kOpenPackageResourceFile = 0x66E820;
inline constexpr std::uint32_t kTextScriptInterpreterInitialize = 0x7E0DA0;
inline constexpr std::uint32_t kCombatHandleAction = 0x57C4B0;
inline constexpr std::uint32_t kCombatCreateStuntAction = 0x566D10;
inline constexpr std::uint32_t kCombatExecuteStunt = 0x55B9E0;
inline constexpr std::uint32_t kCombatSkillDamage = 0x57D520;
inline constexpr std::uint32_t kCombatSystemEnd = 0x575ED0;
inline constexpr std::uint32_t kGameDbGetInstanceInternal = 0x50C770;
inline constexpr std::uint32_t kGameDbFindStuntById = 0x4F3960;
inline constexpr std::uint32_t kAiSelectStuntBegin = 0x5621C0;
inline constexpr std::uint32_t kAiSelectStuntEnd = 0x562223;
inline constexpr std::uint32_t kCrtRuntimeMessage = 0x744A95;
inline constexpr std::uint32_t kCrtMessageBox = 0x748CD0;
inline constexpr std::uint32_t kMovementCollisionCheck = 0x5FF680;

inline constexpr std::uint32_t kMapVirtualKeyToUiKey = 0x412130;
inline constexpr std::uint32_t kEnableMouseCapture = 0x4120D0;
inline constexpr std::uint32_t kDisableMouseCapture = 0x4120E0;
inline constexpr std::uint32_t kSetRenderStates = 0x4149D0;
inline constexpr std::uint32_t kRenderGeometryAndResetCounter = 0x414A20;
inline constexpr std::uint32_t kPalGameIvInitCameraSubsystem = 0x5EADE0;
inline constexpr std::uint32_t kCameraGetActiveCameraInternalId = 0x5EBA20;
inline constexpr std::uint32_t kCameraSetMode = 0x5EB960;
inline constexpr std::uint32_t kCameraSetYaw = 0x5E9D20;
inline constexpr std::uint32_t kCameraSetPitch = 0x5E9D70;
inline constexpr std::uint32_t kCameraSetDistance = 0x5E9CC0;

inline constexpr std::uint32_t kPalGameIvGetInstance = pal4::ida::kPalGameIvGetInstance;
inline constexpr std::uint32_t kUiFrameManagerGetInstance = pal4::ida::kUiFrameManagerGetInstance;
inline constexpr std::uint32_t kGameConfigGlobal = 0x8E3D90;
inline constexpr std::uint32_t kIsCsbModeGlobal = 0x8C27FC;
inline constexpr std::uint32_t kD3d9MaxMsaaTypeGlobal = 0x8D5724;
inline constexpr std::uint32_t kD3d9RequestedMsaaTypeGlobal = 0x8D5728;
inline constexpr std::uint32_t kD3d9MaxMsaaQualityLevelsGlobal = 0x8D572C;
inline constexpr std::uint32_t kD3d9RequestedNonMaskableQualityGlobal = 0x8D5730;
inline constexpr std::uint32_t kD3d9PresentMultiSampleTypeGlobal = 0x97A4F0;
inline constexpr std::uint32_t kD3d9PresentMultiSampleQualityGlobal = 0x97A4F4;
inline constexpr std::uint32_t kRenderStateInterfaceGlobal = 0x950CD0;
inline constexpr std::uint32_t kKeyCodeTable = 0x8A1790;

inline constexpr std::ptrdiff_t kUiFrameManagerProcessUiEventThisOffset = 408;
inline constexpr std::ptrdiff_t kUiFrameManagerMessageHandledByteOffset = 544;
inline constexpr std::ptrdiff_t kUiFrameManagerEscapeSimulationByteOffset = 545;
inline constexpr std::ptrdiff_t kPalGameIvCurrentStateEntryIndex = pal4::ida::kPalGameIvCurrentStateEntryIndex;

inline constexpr std::uintptr_t ResolveRuntimeAddress(
    const std::uintptr_t module_base,
    const std::uint32_t ida_ea) noexcept {
    return module_base + static_cast<std::uintptr_t>(ida_ea - kLaunchExeBase);
}

}  // namespace pal4::inject::ida

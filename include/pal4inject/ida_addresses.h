#pragma once

#include <cstddef>
#include <cstdint>

#include "pal4/runtime_layouts.h"

namespace pal4::inject::ida {

inline constexpr std::uint32_t kLaunchExeBase = pal4::ida::kLaunchExeBase;

inline constexpr std::uint32_t kProcessUiEvent = 0x411900;
inline constexpr std::uint32_t kHandleUiMessageAndProcess = 0x4BF0E0;
inline constexpr std::uint32_t kSimulateKeyPressAndRelease = 0x4BFD70;
inline constexpr std::uint32_t kProcessInputs = 0x4080B0;
inline constexpr std::uint32_t kUpdateInputDeviceState = 0x407910;
inline constexpr std::uint32_t kInitializeDirectInput = 0x407640;
inline constexpr std::uint32_t kGiTalkScriptCallback = 0x5DFB10;
inline constexpr std::uint32_t kCeguiRendererConstructor2 = 0x413580;
inline constexpr std::uint32_t kCeguiSystemInitialize = 0x410450;
inline constexpr std::uint32_t kSetupMinimapTexture = 0x40DE10;
inline constexpr std::uint32_t kCameraUpdateMatrix = 0x5EA190;
inline constexpr std::uint32_t kD3d9SetPresentParameters = 0x75F710;
inline constexpr std::uint32_t kPal4MainWndProc = 0x40A170;
inline constexpr std::uint32_t kHandlePlayerInputEvents = 0x4283B0;

inline constexpr std::uint32_t kMapVirtualKeyToUiKey = 0x412130;
inline constexpr std::uint32_t kEnableMouseCapture = 0x4120D0;
inline constexpr std::uint32_t kDisableMouseCapture = 0x4120E0;
inline constexpr std::uint32_t kTransformMouseCoordinates = 0x412320;
inline constexpr std::uint32_t kSetRenderStates = 0x4149D0;
inline constexpr std::uint32_t kRenderGeometryAndResetCounter = 0x414A20;
inline constexpr std::uint32_t kPalGameIvInitCameraSubsystem = 0x5EADE0;
inline constexpr std::uint32_t kCameraGetActiveCameraInternalId = 0x5EBA20;

inline constexpr std::uint32_t kPalGameIvGetInstance = pal4::ida::kPalGameIvGetInstance;
inline constexpr std::uint32_t kUiFrameManagerGetInstance = pal4::ida::kUiFrameManagerGetInstance;
inline constexpr std::uint32_t kGameConfigGlobal = 0x8E3D90;
inline constexpr std::uint32_t kD3d9MaxMsaaTypeGlobal = 0x8D5724;
inline constexpr std::uint32_t kD3d9RequestedMsaaTypeGlobal = 0x8D5728;
inline constexpr std::uint32_t kD3d9MaxMsaaQualityLevelsGlobal = 0x8D572C;
inline constexpr std::uint32_t kD3d9RequestedNonMaskableQualityGlobal = 0x8D5730;
inline constexpr std::uint32_t kD3d9PresentMultiSampleTypeGlobal = 0x97A4F0;
inline constexpr std::uint32_t kD3d9PresentMultiSampleQualityGlobal = 0x97A4F4;
inline constexpr std::uint32_t kRenderStateInterfaceGlobal = 0x950CD0;

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

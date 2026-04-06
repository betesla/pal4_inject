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
inline constexpr std::uint32_t kPal4MainWndProc = 0x40A170;
inline constexpr std::uint32_t kHandlePlayerInputEvents = 0x4283B0;

inline constexpr std::uint32_t kMapVirtualKeyToUiKey = 0x412130;
inline constexpr std::uint32_t kEnableMouseCapture = 0x4120D0;
inline constexpr std::uint32_t kDisableMouseCapture = 0x4120E0;
inline constexpr std::uint32_t kTransformMouseCoordinates = 0x412320;

inline constexpr std::uint32_t kPalGameIvGetInstance = pal4::ida::kPalGameIvGetInstance;
inline constexpr std::uint32_t kUiFrameManagerGetInstance = pal4::ida::kUiFrameManagerGetInstance;

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

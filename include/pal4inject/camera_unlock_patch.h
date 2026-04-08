#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace pal4::inject {

struct CameraAngleLimitPatch {
    std::uint32_t instruction_ea = 0;
    std::array<std::uint8_t, 2> expected_opcode_bytes{};
    std::uint32_t expected_operand_ea = 0;
    std::uintptr_t replacement_operand = 0;
};

std::vector<CameraAngleLimitPatch> BuildCameraPitchUnlockPatches();

struct CameraInputScalePatch {
    std::uint32_t instruction_ea = 0;
    std::array<std::uint8_t, 3> expected_bytes{};
    std::uint8_t replacement_displacement = 0;
};

std::vector<CameraInputScalePatch> BuildCameraInputScalePatches();

extern const float g_camera_pitch_limit_negative;
extern const float g_camera_pitch_limit_positive;

}  // namespace pal4::inject

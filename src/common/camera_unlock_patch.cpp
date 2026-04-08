#include "pal4inject/camera_unlock_patch.h"

#include "pal4inject/ida_addresses.h"

namespace pal4::inject {

std::vector<CameraAngleLimitPatch> BuildCameraPitchUnlockPatches() {
    return {
        {
            0x424C1A,
            {0xD8, 0x15},
            0x842690,  // -60.0f in current binary
            reinterpret_cast<std::uintptr_t>(&g_camera_pitch_limit_negative),
        },
        {
            0x424C91,
            {0xD8, 0x25},
            0x841E5C,  // +20.0f
            reinterpret_cast<std::uintptr_t>(&g_camera_pitch_limit_positive),
        },
        {
            0x424CB5,
            {0xD8, 0x1D},
            0x841E5C,  // +20.0f
            reinterpret_cast<std::uintptr_t>(&g_camera_pitch_limit_positive),
        },
        {
            0x424CC7,
            {0xD8, 0x05},
            0x841E5C,  // +20.0f
            reinterpret_cast<std::uintptr_t>(&g_camera_pitch_limit_positive),
        },
    };
}

std::vector<CameraInputScalePatch> BuildCameraInputScalePatches() {
    return {
        {
            0x424BFF,
            {0xD8, 0x4E, 0x30},  // fmul dword ptr [esi+30h]
            0x34,                // match horizontal scale slot [esi+34h]
        },
    };
}

}  // namespace pal4::inject

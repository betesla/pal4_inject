#include "pal4inject/camera_pitch_guard.h"

#include <cmath>

namespace pal4::inject {

float NormalizeAngle360(const float angle_degrees) noexcept {
    float normalized = std::fmod(angle_degrees, 360.0F);
    if (normalized < 0.0F) {
        normalized += 360.0F;
    }
    if (normalized >= 360.0F) {
        normalized -= 360.0F;
    }
    return normalized;
}

bool IsSafeCameraPitchAngle(const float normalized_angle_degrees) noexcept {
    return normalized_angle_degrees <= kCameraPitchPositiveLimitNormalized ||
        normalized_angle_degrees >= kCameraPitchNegativeLimitNormalized;
}

float ClampCameraPitchAngle(const float angle_degrees) noexcept {
    const float normalized = NormalizeAngle360(angle_degrees);
    if (IsSafeCameraPitchAngle(normalized)) {
        return normalized;
    }

    const float distance_to_positive_limit =
        normalized - kCameraPitchPositiveLimitNormalized;
    const float distance_to_negative_limit =
        kCameraPitchNegativeLimitNormalized - normalized;
    return distance_to_positive_limit <= distance_to_negative_limit
        ? kCameraPitchPositiveLimitNormalized
        : kCameraPitchNegativeLimitNormalized;
}

}  // namespace pal4::inject

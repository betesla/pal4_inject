#pragma once

namespace pal4::inject {

inline constexpr float kCameraPitchLimitDegrees = 89.0F;
inline constexpr float kCameraPitchPositiveLimitNormalized = kCameraPitchLimitDegrees;
inline constexpr float kCameraPitchNegativeLimitNormalized = 360.0F - kCameraPitchLimitDegrees;

float NormalizeAngle360(float angle_degrees) noexcept;
bool IsSafeCameraPitchAngle(float normalized_angle_degrees) noexcept;
float ClampCameraPitchAngle(float angle_degrees) noexcept;

}  // namespace pal4::inject

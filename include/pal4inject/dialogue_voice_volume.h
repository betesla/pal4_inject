#pragma once

#include <cstdint>
#include <string_view>

namespace pal4::inject {

constexpr float kDefaultGiTalkVolume = 1.0F;
constexpr float kMinGiTalkVolume = 0.0F;
constexpr float kMaxGiTalkVolume = 3.0F;
constexpr float kGiTalkVolumeHotkeyStep = 0.05F;
constexpr std::uint64_t kGiTalkVolumeOsdDurationMs = 1600;
constexpr std::uint64_t kGiTalkVolumeOsdFadeMs = 350;

struct GiTalkVolumeHotkeyDecision {
    bool consume = false;
    int step_direction = 0;
};

float ClampGiTalkVolume(float volume) noexcept;
bool TryParseGiTalkVolume(std::string_view text, float* out);
bool IsGiTalkVoiceResource(std::string_view resource);
GiTalkVolumeHotkeyDecision ResolveGiTalkVolumeHotkey(
    std::uint32_t message,
    std::uint32_t virtual_key,
    bool auto_repeat) noexcept;
float StepGiTalkVolume(float current, int step_direction) noexcept;
int GiTalkVolumeWaveCount(float volume) noexcept;
float ComputeGiTalkVolumeOsdOpacity(std::uint64_t elapsed_ms) noexcept;

}  // namespace pal4::inject

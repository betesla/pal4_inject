#include "pal4inject/dialogue_voice_volume.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>

namespace pal4::inject {

float ClampGiTalkVolume(const float volume) noexcept {
    if (!std::isfinite(volume)) {
        return kDefaultGiTalkVolume;
    }
    return std::clamp(volume, kMinGiTalkVolume, kMaxGiTalkVolume);
}

bool TryParseGiTalkVolume(const std::string_view text, float* const out) {
    if (!out || text.empty()) {
        return false;
    }

    const std::string value(text);
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(value.c_str(), &end);
    if (errno == ERANGE || end != value.c_str() + value.size() ||
        !std::isfinite(parsed) || parsed < kMinGiTalkVolume ||
        parsed > kMaxGiTalkVolume) {
        return false;
    }
    *out = parsed;
    return true;
}

bool IsGiTalkVoiceResource(const std::string_view resource) {
    std::string lowered(resource);
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered.find("palsound") != std::string::npos && lowered.ends_with(".mp3");
}

GiTalkVolumeHotkeyDecision ResolveGiTalkVolumeHotkey(
    const std::uint32_t message,
    const std::uint32_t virtual_key,
    const bool auto_repeat) noexcept {
    constexpr std::uint32_t kWmKeyDown = 0x0100;
    constexpr std::uint32_t kWmKeyUp = 0x0101;
    constexpr std::uint32_t kWmChar = 0x0102;
    constexpr std::uint32_t kWmSysKeyDown = 0x0104;
    constexpr std::uint32_t kWmSysKeyUp = 0x0105;
    constexpr std::uint32_t kWmSysChar = 0x0106;
    constexpr std::uint32_t kVkOemOpenBrackets = 0xDB;
    constexpr std::uint32_t kVkOemCloseBrackets = 0xDD;
    constexpr std::uint32_t kOpenBracketCharacter = '[';
    constexpr std::uint32_t kCloseBracketCharacter = ']';

    const bool is_key_message = message == kWmKeyDown || message == kWmKeyUp ||
        message == kWmSysKeyDown || message == kWmSysKeyUp;
    const bool is_character_message = message == kWmChar || message == kWmSysChar;
    const bool is_hotkey_key = is_key_message &&
        (virtual_key == kVkOemOpenBrackets ||
         virtual_key == kVkOemCloseBrackets);
    const bool is_hotkey_character = is_character_message &&
        (virtual_key == kOpenBracketCharacter || virtual_key == kCloseBracketCharacter);
    if (!is_hotkey_key && !is_hotkey_character) {
        return {};
    }

    GiTalkVolumeHotkeyDecision decision{};
    decision.consume = true;
    const bool key_down = is_key_message &&
        (message == kWmKeyDown || message == kWmSysKeyDown);
    if (key_down && !auto_repeat) {
        decision.step_direction =
            virtual_key == kVkOemOpenBrackets ? -1 : 1;
    }
    return decision;
}

float StepGiTalkVolume(const float current, const int step_direction) noexcept {
    const int current_percent = static_cast<int>(
        std::lround(ClampGiTalkVolume(current) * 100.0F));
    const int step_percent = static_cast<int>(
        std::lround(kGiTalkVolumeHotkeyStep * 100.0F));
    const int max_percent = static_cast<int>(
        std::lround(kMaxGiTalkVolume * 100.0F));
    const int direction = step_direction < 0 ? -1 : (step_direction > 0 ? 1 : 0);
    const int stepped_percent = std::clamp(
        current_percent + direction * step_percent,
        0,
        max_percent);
    return static_cast<float>(stepped_percent) / 100.0F;
}

int GiTalkVolumeWaveCount(const float volume) noexcept {
    const float clamped = ClampGiTalkVolume(volume);
    if (clamped <= 0.0F) {
        return 0;
    }
    return std::clamp(
        static_cast<int>(std::ceil(clamped / kMaxGiTalkVolume * 3.0F)),
        1,
        3);
}

float ComputeGiTalkVolumeOsdOpacity(const std::uint64_t elapsed_ms) noexcept {
    if (elapsed_ms >= kGiTalkVolumeOsdDurationMs) {
        return 0.0F;
    }
    const std::uint64_t fade_start =
        kGiTalkVolumeOsdDurationMs - kGiTalkVolumeOsdFadeMs;
    if (elapsed_ms <= fade_start) {
        return 1.0F;
    }
    return static_cast<float>(kGiTalkVolumeOsdDurationMs - elapsed_ms) /
        static_cast<float>(kGiTalkVolumeOsdFadeMs);
}

}  // namespace pal4::inject

#include "dialogue_voice_osd.h"

#include <atomic>
#include <cmath>
#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pal4inject/dialogue_voice_volume.h"

namespace pal4::inject {
namespace {

std::atomic<std::uint32_t> g_volume_percent{100};
std::atomic<ULONGLONG> g_shown_tick{0};

}  // namespace

void ShowGiTalkVolumeOsd(const float volume) noexcept {
    const auto percent = static_cast<std::uint32_t>(
        std::lround(ClampGiTalkVolume(volume) * 100.0F));
    g_volume_percent.store(percent, std::memory_order_relaxed);
    g_shown_tick.store(GetTickCount64(), std::memory_order_release);
}

bool TryGetGiTalkVolumeOsdSnapshot(GiTalkVolumeOsdSnapshot* const out) noexcept {
    if (!out) {
        return false;
    }
    const ULONGLONG shown_tick = g_shown_tick.load(std::memory_order_acquire);
    if (shown_tick == 0) {
        return false;
    }
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG elapsed = now >= shown_tick ? now - shown_tick : 0;
    const float opacity = ComputeGiTalkVolumeOsdOpacity(elapsed);
    if (opacity <= 0.0F) {
        return false;
    }
    out->volume = static_cast<float>(
        g_volume_percent.load(std::memory_order_relaxed)) / 100.0F;
    out->opacity = opacity;
    return true;
}

}  // namespace pal4::inject

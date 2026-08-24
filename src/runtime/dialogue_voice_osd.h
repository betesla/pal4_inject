#pragma once

namespace pal4::inject {

struct GiTalkVolumeOsdSnapshot {
    float volume = 1.0F;
    float opacity = 0.0F;
};

void ShowGiTalkVolumeOsd(float volume) noexcept;
bool TryGetGiTalkVolumeOsdSnapshot(GiTalkVolumeOsdSnapshot* out) noexcept;

}  // namespace pal4::inject

#pragma once

#include <cstdint>
#include <string>

#include "pal4inject/types.h"

namespace pal4::inject {

struct ShadowQualitySnapshot {
    ShadowResolution desired_resolution = ShadowResolution::x64;
    std::uint32_t quality_enable = 0;
    std::uint32_t resolution_shift = 0;
    std::uint32_t raster_size = 0;
    std::uint32_t blur_pass_count = 0;
    std::uint32_t half_res_enabled = 0;
    bool globals_available = false;
};

bool ApplyShadowResolutionGlobals(ShadowResolution resolution, std::string* error);
bool BuildShadowQualitySnapshot(ShadowQualitySnapshot* out);
std::string DescribeShadowQualityState(const ShadowQualitySnapshot& snapshot);

}  // namespace pal4::inject

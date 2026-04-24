#include "shadow_quality_hooks.h"

#include <sstream>

#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

struct ShadowResolutionConfig {
    std::uint32_t quality_enable = 1;
    std::uint32_t resolution_shift = 6;
    std::uint32_t blur_pass_count = 2;
    std::uint32_t half_res_enabled = 1;
};

ShadowResolutionConfig ConfigForShadowResolution(const ShadowResolution resolution) {
    switch (resolution) {
    case ShadowResolution::x64:
        return {1, 6, 2, 1};
    case ShadowResolution::x128:
        return {1, 7, 1, 0};
    case ShadowResolution::x256:
        return {1, 8, 0, 0};
    case ShadowResolution::x512:
        return {1, 9, 0, 0};
    }
    return {1, 6, 2, 1};
}

std::uint32_t* ResolveRuntimeU32(const std::uint32_t ida_ea) {
    const auto base = GetRuntimeState().MainModuleBase();
    if (base == 0) {
        return nullptr;
    }
    return reinterpret_cast<std::uint32_t*>(ida::ResolveRuntimeAddress(base, ida_ea));
}

}  // namespace

bool ApplyShadowResolutionGlobals(const ShadowResolution resolution, std::string* error) {
    auto* quality_enable = ResolveRuntimeU32(ida::kShadowQualityEnableGlobal);
    auto* resolution_shift = ResolveRuntimeU32(ida::kShadowResolutionShiftGlobal);
    auto* blur_pass_count = ResolveRuntimeU32(ida::kShadowBlurPassCountGlobal);
    auto* half_res_enabled = ResolveRuntimeU32(ida::kShadowHalfResGlobal);
    if (!quality_enable || !resolution_shift || !blur_pass_count || !half_res_enabled) {
        if (error) {
            *error = "shadow-quality globals are unavailable";
        }
        return false;
    }

    const auto config = ConfigForShadowResolution(resolution);
    *quality_enable = config.quality_enable;
    *resolution_shift = config.resolution_shift;
    *blur_pass_count = config.blur_pass_count;
    *half_res_enabled = config.half_res_enabled;
    if (error) {
        error->clear();
    }
    return true;
}

bool BuildShadowQualitySnapshot(ShadowQualitySnapshot* out) {
    if (!out) {
        return false;
    }

    *out = {};
    out->desired_resolution = GetRuntimeState().GetShadowResolution();
    const auto quality_enable = ResolveRuntimeU32(ida::kShadowQualityEnableGlobal);
    const auto resolution_shift = ResolveRuntimeU32(ida::kShadowResolutionShiftGlobal);
    const auto blur_pass_count = ResolveRuntimeU32(ida::kShadowBlurPassCountGlobal);
    const auto half_res_enabled = ResolveRuntimeU32(ida::kShadowHalfResGlobal);
    if (!quality_enable || !resolution_shift || !blur_pass_count || !half_res_enabled) {
        return false;
    }

    out->quality_enable = *quality_enable;
    out->resolution_shift = *resolution_shift;
    out->raster_size = 1u << *resolution_shift;
    out->blur_pass_count = *blur_pass_count;
    out->half_res_enabled = *half_res_enabled;
    out->globals_available = true;
    return true;
}

std::string DescribeShadowQualityState(const ShadowQualitySnapshot& snapshot) {
    std::ostringstream out;
    out << "Desired " << ToString(snapshot.desired_resolution)
        << " (" << (1u << ConfigForShadowResolution(snapshot.desired_resolution).resolution_shift)
        << "x" << (1u << ConfigForShadowResolution(snapshot.desired_resolution).resolution_shift)
        << ")";
    if (!snapshot.globals_available) {
        out << " | waiting for runtime";
        return out.str();
    }

    out << " | Raster " << snapshot.raster_size << "x" << snapshot.raster_size
        << " | HalfRes " << (snapshot.half_res_enabled != 0 ? "on" : "off")
        << " | Blur " << snapshot.blur_pass_count
        << " | Quality " << (snapshot.quality_enable != 0 ? "on" : "off");
    return out.str();
}

}  // namespace pal4::inject

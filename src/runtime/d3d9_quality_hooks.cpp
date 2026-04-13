#include "d3d9_quality_hooks.h"

#include <sstream>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "hook_logging.h"
#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using D3d9SetPresentParametersFn = int (__cdecl*)(int*, int, int);

D3d9SetPresentParametersFn g_original_d3d9_set_present_parameters = nullptr;

std::uint32_t RequestedMsaaTypeForLevel(const MsaaLevel level) noexcept {
    switch (level) {
    case MsaaLevel::x2:
        return 2;
    case MsaaLevel::x4:
        return 4;
    case MsaaLevel::x8:
        return 8;
    case MsaaLevel::off:
        return 1;
    }
    return 1;
}

std::string DescribeMsaaType(const std::uint32_t type) {
    switch (type) {
    case 0:
    case 1:
        return "off";
    case 2:
        return "2x";
    case 4:
        return "4x";
    case 8:
        return "8x";
    default:
        return std::to_string(type) + "x";
    }
}

int* ResolveRuntimeInt(const std::uint32_t ida_ea) {
    const auto base = GetRuntimeState().MainModuleBase();
    if (base == 0) {
        return nullptr;
    }
    return reinterpret_cast<int*>(ida::ResolveRuntimeAddress(base, ida_ea));
}

bool ForcePresentParametersMsaa(
    const MsaaLevel level,
    std::string* error) {
    auto* present_type = ResolveRuntimeInt(ida::kD3d9PresentMultiSampleTypeGlobal);
    auto* present_quality = ResolveRuntimeInt(ida::kD3d9PresentMultiSampleQualityGlobal);
    auto* max_type = ResolveRuntimeInt(ida::kD3d9MaxMsaaTypeGlobal);
    auto* max_quality_levels = ResolveRuntimeInt(ida::kD3d9MaxMsaaQualityLevelsGlobal);
    if (!present_type || !present_quality || !max_type || !max_quality_levels) {
        if (error) {
            *error = "D3D9 present-parameter globals are unavailable";
        }
        return false;
    }

    if (level == MsaaLevel::off) {
        *present_type = 0;
        *present_quality = 0;
        if (error) {
            error->clear();
        }
        return true;
    }

    const int requested_type = static_cast<int>(RequestedMsaaTypeForLevel(level));
    const int supported_type = *max_type;
    const int supported_quality_levels = *max_quality_levels;
    if (supported_type <= 1) {
        if (error) {
            *error = "runtime reports no multisample support for the current mode";
        }
        return false;
    }

    *present_type = requested_type <= supported_type ? requested_type : supported_type;
    *present_quality =
        supported_quality_levels > 0 ? 0 : 0;
    if (error) {
        error->clear();
    }
    return true;
}

void LogMsaaEvent(
    const MsaaLevel level,
    const HookMode mode,
    const int result) {
    D3d9MsaaSnapshot snapshot{};
    BuildD3d9MsaaSnapshot(&snapshot);
    std::ostringstream out;
    out << "hook=d3d9_set_present_parameters"
        << " mode=" << ToString(mode)
        << " msaa=" << ToString(level)
        << " requested_type=" << snapshot.requested_type
        << " present_type=" << snapshot.applied_type
        << " present_quality=" << snapshot.applied_quality
        << " result=" << result;
    AppendHookEventLog(HookId::d3d9_set_present_parameters, out.str());
}

}  // namespace

bool ApplyRequestedMsaaLevel(const MsaaLevel level, std::string* error) {
    auto* type_ptr = ResolveRuntimeInt(ida::kD3d9RequestedMsaaTypeGlobal);
    auto* quality_ptr = ResolveRuntimeInt(ida::kD3d9RequestedNonMaskableQualityGlobal);
    if (!type_ptr || !quality_ptr) {
        if (error) {
            *error = "D3D9 multisample globals are unavailable";
        }
        return false;
    }

    *type_ptr = static_cast<int>(RequestedMsaaTypeForLevel(level));
    *quality_ptr = 1;
    if (error) {
        error->clear();
    }
    return true;
}

bool BuildD3d9MsaaSnapshot(D3d9MsaaSnapshot* out) {
    if (!out) {
        return false;
    }

    *out = {};
    out->desired_level = GetRuntimeState().GetMsaaLevel();
    const auto requested_type = ResolveRuntimeInt(ida::kD3d9RequestedMsaaTypeGlobal);
    const auto applied_type = ResolveRuntimeInt(ida::kD3d9PresentMultiSampleTypeGlobal);
    const auto applied_quality = ResolveRuntimeInt(ida::kD3d9PresentMultiSampleQualityGlobal);
    const auto max_type = ResolveRuntimeInt(ida::kD3d9MaxMsaaTypeGlobal);
    const auto max_quality = ResolveRuntimeInt(ida::kD3d9MaxMsaaQualityLevelsGlobal);
    if (!requested_type || !applied_type || !applied_quality || !max_type || !max_quality) {
        return false;
    }

    out->requested_type = static_cast<std::uint32_t>(*requested_type);
    out->applied_type = static_cast<std::uint32_t>(*applied_type);
    out->applied_quality = static_cast<std::uint32_t>(*applied_quality);
    out->max_supported_type = static_cast<std::uint32_t>(*max_type);
    out->max_supported_quality = static_cast<std::uint32_t>(*max_quality);
    out->globals_available = true;
    return true;
}

std::string DescribeMsaaState(const D3d9MsaaSnapshot& snapshot) {
    std::ostringstream out;
    out << "Desired " << ToString(snapshot.desired_level);
    if (!snapshot.globals_available) {
        out << " | waiting for D3D9";
        return out.str();
    }

    out << " | Applied " << DescribeMsaaType(snapshot.applied_type);
    if (snapshot.applied_quality > 0) {
        out << " q" << snapshot.applied_quality;
    }
    out << " | Max " << DescribeMsaaType(snapshot.max_supported_type);
    return out.str();
}

int __cdecl Hook_D3d9SetPresentParameters(
    int* display_mode,
    int fullscreen,
    int format) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::d3d9_set_present_parameters);

    if (!g_original_d3d9_set_present_parameters) {
        state.SetHookError(
            HookId::d3d9_set_present_parameters,
            "original D3D9SetPresentParameters trampoline is null");
        state.SetLastError("original D3D9SetPresentParameters trampoline is null");
        return 0;
    }

    const HookMode mode = state.GetHookMode(HookId::d3d9_set_present_parameters);
    const MsaaLevel level = state.GetMsaaLevel();
    if (mode != HookMode::observe_only && mode != HookMode::mirror_compare) {
        std::string error;
        if (!ApplyRequestedMsaaLevel(level, &error)) {
            state.SetHookError(HookId::d3d9_set_present_parameters, error);
            state.SetLastError(error);
        } else {
            state.ClearHookError(HookId::d3d9_set_present_parameters);
        }
    }

    const int result =
        g_original_d3d9_set_present_parameters(display_mode, fullscreen, format);
    if (mode != HookMode::observe_only && mode != HookMode::mirror_compare) {
        std::string error;
        if (!ForcePresentParametersMsaa(level, &error)) {
            state.SetHookError(HookId::d3d9_set_present_parameters, error);
            state.SetLastError(error);
        } else {
            state.ClearHookError(HookId::d3d9_set_present_parameters);
        }
    }
    LogMsaaEvent(level, mode, result);
    return result;
}

void* GetD3d9QualityReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::d3d9_set_present_parameters:
        return reinterpret_cast<void*>(&Hook_D3d9SetPresentParameters);
    default:
        return nullptr;
    }
}

void SetD3d9QualityOriginalTrampoline(const HookId id, void* trampoline) {
    switch (id) {
    case HookId::d3d9_set_present_parameters:
        g_original_d3d9_set_present_parameters =
            reinterpret_cast<D3d9SetPresentParametersFn>(trampoline);
        break;
    default:
        break;
    }
}

}  // namespace pal4::inject

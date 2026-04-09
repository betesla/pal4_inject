#pragma once

#include <string>

#include "pal4inject/types.h"

namespace pal4::inject {

struct D3d9MsaaSnapshot {
    MsaaLevel desired_level = MsaaLevel::off;
    std::uint32_t requested_type = 0;
    std::uint32_t applied_type = 0;
    std::uint32_t applied_quality = 0;
    std::uint32_t max_supported_type = 0;
    std::uint32_t max_supported_quality = 0;
    bool globals_available = false;
};

void* GetD3d9QualityReplacementForHook(HookId id);
void SetD3d9QualityOriginalTrampoline(HookId id, void* trampoline);
bool ApplyRequestedMsaaLevel(MsaaLevel level, std::string* error);
bool BuildD3d9MsaaSnapshot(D3d9MsaaSnapshot* out);
std::string DescribeMsaaState(const D3d9MsaaSnapshot& snapshot);

}  // namespace pal4::inject

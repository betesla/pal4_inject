#pragma once

#include <array>
#include <string_view>

#include "pal4/evidence_status.h"

namespace pal4 {

struct CompilerEvidencePoint {
    std::string_view topic;
    std::string_view address;
    std::string_view detail;
    EvidenceStatus status;
};

struct CompilerProfile {
    std::string_view family;
    std::string_view likely_generation;
    std::string_view likely_toolset;
    EvidenceStatus generation_status;
    bool vcpp70_confirmed;
};

const CompilerProfile& GetCompilerProfile() noexcept;
const std::array<CompilerEvidencePoint, 6>& GetCompilerEvidence() noexcept;

}  // namespace pal4

#pragma once

#include <string_view>

namespace pal4 {

enum class EvidenceStatus {
    verified_in_ida,
    partially_verified,
    likely,
    uncertain,
    hypothesis,
    obsolete,
};

constexpr std::string_view ToString(EvidenceStatus status) noexcept {
    switch (status) {
    case EvidenceStatus::verified_in_ida:
        return "verified_in_ida";
    case EvidenceStatus::partially_verified:
        return "partially_verified";
    case EvidenceStatus::likely:
        return "likely";
    case EvidenceStatus::uncertain:
        return "uncertain";
    case EvidenceStatus::hypothesis:
        return "hypothesis";
    case EvidenceStatus::obsolete:
        return "obsolete";
    }
    return "uncertain";
}

}  // namespace pal4

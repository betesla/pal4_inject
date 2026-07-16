#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "pal4/evidence_status.h"

namespace pal4 {

struct CeguiRendererVirtualEvidence {
    std::string_view name;
    EvidenceStatus source_status;
    EvidenceStatus pal4_status;
    std::string_view note;
};

struct CeguiRendererVtableSlotEvidence {
    int slot;
    std::string_view name;
    EvidenceStatus status;
    std::string_view note;
};

inline constexpr std::array<CeguiRendererVirtualEvidence, 12> kCeguiRendererVirtualEvidence = {{
    {"addQuad", EvidenceStatus::verified_in_ida, EvidenceStatus::hypothesis, "pure virtual in CEGUI 0.4.1 base Renderer; PAL4 derived renderer must provide implementation"},
    {"doRender", EvidenceStatus::verified_in_ida, EvidenceStatus::hypothesis, "pure virtual in CEGUI 0.4.1 base Renderer; likely touched during System startup and later frame presentation"},
    {"clearRenderList", EvidenceStatus::verified_in_ida, EvidenceStatus::hypothesis, "pure virtual queue maintenance path"},
    {"setQueueingEnabled", EvidenceStatus::verified_in_ida, EvidenceStatus::hypothesis, "queue mode toggle used by concrete renderers"},
    {"createTexture_empty", EvidenceStatus::verified_in_ida, EvidenceStatus::hypothesis, "pure virtual texture factory path"},
    {"createTexture_file", EvidenceStatus::verified_in_ida, EvidenceStatus::hypothesis, "pure virtual file-backed texture creation path"},
    {"createTexture_sized", EvidenceStatus::verified_in_ida, EvidenceStatus::hypothesis, "pure virtual sized texture creation path"},
    {"destroyTexture", EvidenceStatus::verified_in_ida, EvidenceStatus::hypothesis, "pure virtual texture release path"},
    {"destroyAllTextures", EvidenceStatus::verified_in_ida, EvidenceStatus::hypothesis, "pure virtual global texture cleanup path"},
    {"isQueueingEnabled", EvidenceStatus::verified_in_ida, EvidenceStatus::hypothesis, "virtual state query used by concrete renderer and potentially by System"},
    {"getWidth_height_size_rect", EvidenceStatus::verified_in_ida, EvidenceStatus::hypothesis, "virtual display metrics contract consumed by System and layout logic"},
    {"getMaxTextureSize_and_dpi", EvidenceStatus::verified_in_ida, EvidenceStatus::hypothesis, "virtual capabilities contract inherited by concrete renderer"},
}};

inline constexpr std::array<CeguiRendererVtableSlotEvidence, 14> kCeguiRendererAbstractSlotEvidence = {{
    {6, "addQuad", EvidenceStatus::verified_in_ida, "first pure virtual slot after base EventSet / destructor / helper region"},
    {7, "doRender", EvidenceStatus::verified_in_ida, "pure virtual render queue flush"},
    {8, "clearRenderList", EvidenceStatus::verified_in_ida, "pure virtual queue clear"},
    {9, "setQueueingEnabled", EvidenceStatus::verified_in_ida, "pure virtual queue mode toggle"},
    {10, "createTexture_empty", EvidenceStatus::verified_in_ida, "pure virtual empty texture factory"},
    {11, "createTexture_file", EvidenceStatus::verified_in_ida, "pure virtual file-backed texture factory"},
    {12, "createTexture_sized", EvidenceStatus::verified_in_ida, "pure virtual sized texture factory"},
    {13, "destroyTexture", EvidenceStatus::verified_in_ida, "pure virtual texture release"},
    {14, "destroyAllTextures", EvidenceStatus::verified_in_ida, "pure virtual global texture release"},
    {15, "isQueueingEnabled", EvidenceStatus::verified_in_ida, "pure virtual queue state query"},
    {16, "getWidth", EvidenceStatus::verified_in_ida, "pure virtual display width query"},
    {17, "getHeight", EvidenceStatus::verified_in_ida, "pure virtual display height query"},
    {18, "getSize", EvidenceStatus::verified_in_ida, "pure virtual display size query"},
    {19, "getRect", EvidenceStatus::verified_in_ida, "pure virtual display rect query"},
}};

inline constexpr std::array<CeguiRendererVirtualEvidence, 3> kCeguiSystemCtorEarlyRendererDependencies = {{
    {"createResourceProvider", EvidenceStatus::verified_in_ida, EvidenceStatus::likely, "earliest renderer call in CEGUI 0.4.1 System::constructor_impl; base Renderer already provides a default implementation"},
    {"getIdentifierString", EvidenceStatus::verified_in_ida, EvidenceStatus::likely, "used by Logger during constructor_impl completion; base Renderer already stores and returns an identifier string"},
    {"subscribeEvent", EvidenceStatus::verified_in_ida, EvidenceStatus::likely, "renderer inherits EventSet and System subscribes to EventDisplaySizeChanged during constructor_impl"},
}};

inline constexpr std::array<CeguiRendererVirtualEvidence, 2> kCeguiSystemCtorTransitiveRendererDependencies = {{
    {"getRect", EvidenceStatus::verified_in_ida, EvidenceStatus::likely, "MouseCursor singleton constructor immediately queries System::getSingleton().getRenderer()->getRect()"},
    {"getSize", EvidenceStatus::verified_in_ida, EvidenceStatus::likely, "MouseCursor constraint and display-independent position paths rely on renderer size during early singleton setup"},
}};

struct Cegui041CompatibilityAssessment {
    EvidenceStatus source_version_status;
    EvidenceStatus export_surface_status;
    EvidenceStatus binary_identity_status;
    std::string_view note;
};

struct CeguiSystemCtorCoverage {
    std::string_view name;
    bool covered_by_base_shell;
    bool runtime_verified;
    std::string_view note;
};

inline constexpr std::array<CeguiSystemCtorCoverage, 5> kCeguiSystemCtorCoverage = {{
    {"createResourceProvider", true, true, "base Renderer ctor path and source contract indicate this is already covered well enough for early ctor progress"},
    {"getIdentifierString", true, true, "base Renderer stores a default identifier string; current shell exposes the same default"},
    {"subscribeEvent", true, true, "renderer inherits EventSet in CEGUI 0.4.1; no PAL4-specific override evidence needed so far"},
    {"getRect", true, false, "current shell can describe screen rect, but probe evidence still suggests derived vfunc wiring is not yet accepted by the real ctor chain"},
    {"getSize", true, false, "current shell can describe size, but runtime path still fails before System singleton becomes valid"},
}};

struct CeguiRendererVtableScanAssessment {
    EvidenceStatus status;
    int first_repeated_slot;
    int repeated_slot_count;
    std::string_view note;
};

inline constexpr CeguiRendererVtableScanAssessment kCeguiRendererVtableScanAssessment{
    EvidenceStatus::verified_in_ida,
    6,
    14,
    "fresh probe of base CEGUI::Renderer shows a repeated target address from vtable slot 6 onward, consistent with many unresolved abstract slots on the base object",
};

struct CeguiRendererPurecallComparison {
    EvidenceStatus status;
    bool repeated_slot_equals_purecall;
    std::string_view note;
};

inline constexpr CeguiRendererPurecallComparison kCeguiRendererPurecallComparison{
    EvidenceStatus::verified_in_ida,
    false,
    "fresh probe shows the repeated base Renderer vtable slot target is not equal to CRT _purecall, implying an internal DLL thunk or placeholder path instead of a direct CRT purecall entry",
};

struct CeguiRendererMetricsSlotProbeEvidence {
    int slot;
    std::string_view name;
    std::uint32_t seh_code;
    EvidenceStatus status;
};

inline constexpr std::array<CeguiRendererMetricsSlotProbeEvidence, 5> kCeguiRendererMetricsSlotProbeEvidence = {{
    {15, "isQueueingEnabled", 0xC0000002u, EvidenceStatus::verified_in_ida},
    {16, "getWidth", 0xC0000002u, EvidenceStatus::verified_in_ida},
    {17, "getHeight", 0xC0000002u, EvidenceStatus::verified_in_ida},
    {18, "getSize", 0xC0000002u, EvidenceStatus::verified_in_ida},
    {19, "getRect", 0xC0000002u, EvidenceStatus::verified_in_ida},
}};

inline constexpr Cegui041CompatibilityAssessment kCegui041CompatibilityAssessment{
    EvidenceStatus::verified_in_ida,
    EvidenceStatus::likely,
    EvidenceStatus::uncertain,
    "PAL4 CEGUIBase.dll export surface matches key CEGUI 0.4.1 APIs, but full binary/source identity is not yet proven",
};

}  // namespace pal4

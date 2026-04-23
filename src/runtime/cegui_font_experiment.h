#pragma once

#include <string>

namespace pal4::inject {

bool ApplyDynamicFontOversampleExperiment(
    void* font,
    bool enable_rich_text_compensation,
    std::string* error);
bool RestoreDynamicFontOversampleExperiment(void* font, std::string* error);
bool EnsureDialogRichTextGlyphHeightCompensationHook(std::string* error);

}  // namespace pal4::inject

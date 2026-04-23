#pragma once

#include <string>

namespace pal4::inject {

bool ApplyDynamicFontOversampleExperiment(void* font, std::string* error);
bool EnsureDialogRichTextGlyphHeightCompensationHook(std::string* error);

}  // namespace pal4::inject

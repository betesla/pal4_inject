#pragma once

#include <string>
#include <string_view>

#include "pal4inject/ui_snapshot.h"

namespace pal4::inject {

bool CaptureAndCacheUiSnapshot(UiSnapshotTree* out, std::string* error);
bool CopyCachedUiSnapshotNode(
    std::string_view ref,
    UiSnapshotNode* out,
    std::string* error);
bool ClickCachedUiSnapshotRef(std::string_view ref, std::string* error);
bool FillCachedUiSnapshotRef(
    std::string_view ref,
    std::string_view text,
    std::string* error);
bool TypeIntoFocusedUiWindow(std::string_view text, std::string* error);

}  // namespace pal4::inject

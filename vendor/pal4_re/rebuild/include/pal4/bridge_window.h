#pragma once

#include <string>

namespace pal4 {

struct Context;
class PAL4App;
class MainMenuPreviewRenderer;

struct BridgeDebugAidBoundary {
    bool debug_aid_only = true;
    bool proves_real_main_menu = false;
    bool preview_pointer_interaction_enabled = false;
    const char* summary = "bridge_window_is_debug_aid_only";
};

BridgeDebugAidBoundary DescribeBridgeDebugAidBoundary() noexcept;
std::string BuildBridgeDebugAidBanner();
int RunBridgeWindow(Context& ctx, PAL4App& app);

}  // namespace pal4

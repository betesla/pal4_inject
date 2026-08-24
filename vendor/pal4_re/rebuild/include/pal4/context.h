#pragma once

#include <string>
#include <vector>

#include "pal4/compiler_profile.h"
#include "pal4/runtime_config.h"
#include "pal4/window_bridge.h"

namespace pal4 {

struct Context {
    CompilerProfile compiler_profile = GetCompilerProfile();
    GameConfigSnapshot game_config;
    GameStateSnapshot game_state;
    WindowClassPlan window_plan;
    std::vector<std::string> event_log;

    void Log(const std::string& message) {
        event_log.push_back(message);
    }
};

}  // namespace pal4

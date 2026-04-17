#pragma once

#include <filesystem>

namespace pal4::inject {

std::filesystem::path InjectModuleDirectory();
std::filesystem::path InjectDataDirectory();
std::filesystem::path RuntimeLogPath();
std::filesystem::path CrashArtifactDirectory();

}  // namespace pal4::inject

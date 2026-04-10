#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "pal4inject/types.h"

namespace pal4::inject {

inline constexpr char kInjectedScriptModeEnvVar[] = "PAL4_INJECT_SCRIPT_MODE";

std::uintptr_t ResolveScriptModeGlobalAddress(std::uintptr_t module_base) noexcept;
ScriptMode ScriptModeFromCsbFlag(std::uint32_t flag) noexcept;
bool TryReadLocalScriptModeFlag(
    std::uintptr_t module_base,
    std::uint32_t* out_flag) noexcept;
bool TryWriteLocalScriptModeFlag(
    std::uintptr_t module_base,
    ScriptMode mode,
    std::uint32_t* out_flag) noexcept;
std::optional<ScriptMode> LoadInheritedScriptModeOverride(std::string* error);

}  // namespace pal4::inject

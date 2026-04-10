#include "pal4inject/script_mode_override.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <vector>

#include "pal4inject/ida_addresses.h"

namespace pal4::inject {

std::uintptr_t ResolveScriptModeGlobalAddress(const std::uintptr_t module_base) noexcept {
    return ida::ResolveRuntimeAddress(module_base, ida::kIsCsbModeGlobal);
}

ScriptMode ScriptModeFromCsbFlag(const std::uint32_t flag) noexcept {
    return flag == 0 ? ScriptMode::cs : ScriptMode::csb;
}

bool TryReadLocalScriptModeFlag(
    const std::uintptr_t module_base,
    std::uint32_t* out_flag) noexcept {
    if (module_base == 0 || !out_flag) {
        return false;
    }

    const auto address = ResolveScriptModeGlobalAddress(module_base);
    *out_flag = *reinterpret_cast<const volatile std::uint32_t*>(address);
    return true;
}

bool TryWriteLocalScriptModeFlag(
    const std::uintptr_t module_base,
    const ScriptMode mode,
    std::uint32_t* out_flag) noexcept {
    const auto flag = ScriptModeToCsbFlag(mode);
    if (module_base == 0 || !flag.has_value()) {
        return false;
    }

    const auto address = ResolveScriptModeGlobalAddress(module_base);
    *reinterpret_cast<volatile std::uint32_t*>(address) = *flag;

    if (out_flag) {
        *out_flag = *reinterpret_cast<const volatile std::uint32_t*>(address);
        return *out_flag == *flag;
    }
    return true;
}

std::optional<ScriptMode> LoadInheritedScriptModeOverride(std::string* error) {
    const DWORD required = GetEnvironmentVariableA(kInjectedScriptModeEnvVar, nullptr, 0);
    if (required == 0) {
        if (error) {
            error->clear();
        }
        return std::nullopt;
    }

    std::vector<char> buffer(static_cast<std::size_t>(required), '\0');
    const DWORD copied = GetEnvironmentVariableA(
        kInjectedScriptModeEnvVar,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (copied == 0 || copied >= buffer.size()) {
        if (error) {
            *error = std::string("failed to read inherited script mode from ") +
                kInjectedScriptModeEnvVar;
        }
        return std::nullopt;
    }

    ScriptMode mode = ScriptMode::inherit;
    const std::string_view text(buffer.data(), copied);
    if (!TryParseScriptMode(text, &mode) || mode == ScriptMode::inherit) {
        if (error) {
            *error = std::string("invalid inherited script mode: ") + std::string(text);
        }
        return std::nullopt;
    }

    if (error) {
        error->clear();
    }
    return mode;
}

}  // namespace pal4::inject

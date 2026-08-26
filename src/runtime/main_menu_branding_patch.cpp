#include "main_menu_branding_patch.h"

#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pal4inject/ida_addresses.h"
#include "pal4inject/main_menu_branding.h"
#include "runtime_state.h"

namespace pal4::inject {

bool ApplyMainMenuBrandingPatch(std::string* error) {
    auto& state = GetRuntimeState();
    const std::uintptr_t module_base = state.MainModuleBase();
    if (module_base == 0) {
        if (error) {
            *error = "main-menu branding patch has no main module base";
        }
        return false;
    }

    auto* const target = reinterpret_cast<std::uint8_t*>(
        ida::ResolveRuntimeAddress(module_base, ida::kMainMenuVersionText));
    if (std::memcmp(
            target,
            kInjectedMainMenuVersionSlot.data(),
            kInjectedMainMenuVersionSlot.size()) == 0) {
        if (error) {
            error->clear();
        }
        return true;
    }
    if (std::memcmp(
            target,
            kOriginalMainMenuVersionSlot.data(),
            kOriginalMainMenuVersionSlot.size()) != 0) {
        if (error) {
            *error = "main-menu branding source bytes mismatch at IDA 0x8B9494";
        }
        return false;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(
            target,
            kInjectedMainMenuVersionSlot.size(),
            PAGE_READWRITE,
            &old_protect)) {
        if (error) {
            *error = "VirtualProtect failed for main-menu branding patch";
        }
        return false;
    }
    std::memcpy(
        target,
        kInjectedMainMenuVersionSlot.data(),
        kInjectedMainMenuVersionSlot.size());
    DWORD discard = 0;
    VirtualProtect(
        target,
        kInjectedMainMenuVersionSlot.size(),
        old_protect,
        &discard);

    state.AppendEventLog(
        std::string("main_menu_branding_patch=1 text=") +
        std::string(kInjectedMainMenuVersionText));
    if (error) {
        error->clear();
    }
    return true;
}

}  // namespace pal4::inject

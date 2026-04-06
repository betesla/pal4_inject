#pragma once

#include <string>
#include <vector>

#include "pal4inject/types.h"

namespace pal4::inject {

class HookManager {
public:
    bool Initialize(std::string* error);
    bool InstallBootstrapHooks(std::string* error);
    void UninstallAll();
    std::vector<HookDescriptor> CopyInventory() const;

private:
    struct HookRegistration {
        HookDescriptor descriptor{};
        bool install_on_bootstrap = false;
        bool installed = false;
        void* target = nullptr;
        std::vector<std::uint8_t> original_bytes;
    };

    HookRegistration* FindRegistration(HookId id);
    bool InstallHook(HookRegistration& hook, std::string* error);
    void UninstallHook(HookRegistration& hook);

    std::vector<HookRegistration> registrations_;
    bool initialized_ = false;
};

HookManager& GetHookManager();

}  // namespace pal4::inject

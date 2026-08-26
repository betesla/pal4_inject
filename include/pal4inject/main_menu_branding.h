#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pal4::inject {

inline constexpr std::size_t kMainMenuVersionSlotSize = 12;
inline constexpr std::string_view kOriginalMainMenuVersionText = "PAL4 v1.1";
inline constexpr std::string_view kInjectedMainMenuVersionText = "PAL V1.2.1";

constexpr std::array<std::uint8_t, kMainMenuVersionSlotSize>
BuildMainMenuVersionSlot(const std::string_view text) noexcept {
    std::array<std::uint8_t, kMainMenuVersionSlotSize> bytes{};
    const std::size_t copy_size = text.size() < bytes.size() - 1
        ? text.size()
        : bytes.size() - 1;
    for (std::size_t index = 0; index < copy_size; ++index) {
        bytes[index] = static_cast<std::uint8_t>(text[index]);
    }
    return bytes;
}

inline constexpr auto kOriginalMainMenuVersionSlot =
    BuildMainMenuVersionSlot(kOriginalMainMenuVersionText);
inline constexpr auto kInjectedMainMenuVersionSlot =
    BuildMainMenuVersionSlot(kInjectedMainMenuVersionText);

static_assert(
    kInjectedMainMenuVersionText.size() + 1 <= kMainMenuVersionSlotSize,
    "injected main-menu version must fit the original PAL4 data slot");

}  // namespace pal4::inject

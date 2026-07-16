#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include "pal4/evidence_status.h"
#include "pal4/runtime_config.h"

namespace pal4 {

struct WindowBridgeDebugBoundary {
    bool ida_backed_window_plan = true;
    bool proves_real_ui_reached = false;
    bool preview_only_side_channels_allowed = true;
    const char* summary = "window_bridge_plan_is_not_real_ui_proof";
};

WindowBridgeDebugBoundary DescribeWindowBridgeDebugBoundary() noexcept;

enum class WindowStyleKind {
    fullscreen,
    windowed,
    borderless_windowed,
};

// IDA-backed layout evidence for CreateGameWindow (0x409B00).
//
// This is deliberately not a full C++ class with a fixed size. We only record verified
// offsets and provide a conservative "view" wrapper for future wiring.
struct CreateGameWindowHostEvidence {
    static constexpr std::uint32_t kFunctionAddress = 0x409B00;
    static constexpr EvidenceStatus kOffsetsStatus = EvidenceStatus::verified_in_ida;

    // Stores HINSTANCE copied from *(g_GameState + 4).
    static constexpr std::size_t kHinstanceOffset = 0x68;  // +104

    // Stores the HWND returned by CreateWindowExA.
    static constexpr std::size_t kHwndOffset = 0x6C;  // +108

    // Stores HMENU returned by LoadMenuA when lpMenuName != null.
    static constexpr std::size_t kMenuOffset = 0x70;  // +112

    // Set to 1 after MoveWindow completes.
    static constexpr std::size_t kWindowReadyFlagOffset = 0x74;  // +116

    // Destination buffer used by CRT_Strncpy((char*)(this + 120), lpClassName, 0x7F).
    static constexpr std::size_t kClassNameBufferOffset = 0x78;  // +120
    static constexpr std::size_t kClassNameBufferMaxCopy = 0x7F;

    // Client rect output for GetClientRect(hwnd, this + 248).
    static constexpr std::size_t kClientRectOffset = 0xF8;  // +248
    static constexpr std::size_t kRectSize = 0x10;          // 4x int32

    // RECT used by AdjustWindowRect(this + 264, style, 0).
    static constexpr std::size_t kAdjustRectOffset = 0x108;  // +264
    static constexpr std::size_t kAdjustRectLeftOffset = 0x108;    // +264
    static constexpr std::size_t kAdjustRectTopOffset = 0x10C;     // +268
    static constexpr std::size_t kAdjustRectRightOffset = 0x110;   // +272
    static constexpr std::size_t kAdjustRectBottomOffset = 0x114;  // +276

    // Verified region groupings. These are offsets only; the full host object layout is still partial.
    static constexpr std::size_t kHandlesRegionOffset = kHinstanceOffset;
    static constexpr std::size_t kHandlesRegionSize = 0x10;  // [0x68..0x77] inclusive used by 4 dwords

    static constexpr std::size_t kClassNameRegionOffset = kClassNameBufferOffset;
    static constexpr std::size_t kClassNameRegionSize = 0x80;  // conservative: buffer spans at least 0x7F bytes

    static constexpr std::size_t kClientRectRegionOffset = kClientRectOffset;
    static constexpr std::size_t kClientRectRegionSize = kRectSize;

    static constexpr std::size_t kAdjustRectRegionOffset = kAdjustRectOffset;
    static constexpr std::size_t kAdjustRectRegionSize = kRectSize;

    struct VerifiedRegion {
        std::string_view name;
        std::size_t offset;
        std::size_t size;
        EvidenceStatus status;
    };

    static constexpr std::array<VerifiedRegion, 4> VerifiedRegions() noexcept {
        return {{
            {"handles", kHandlesRegionOffset, kHandlesRegionSize, EvidenceStatus::verified_in_ida},
            {"class_name_buf", kClassNameRegionOffset, kClassNameRegionSize, EvidenceStatus::verified_in_ida},
            {"client_rect", kClientRectRegionOffset, kClientRectRegionSize, EvidenceStatus::verified_in_ida},
            {"adjust_rect", kAdjustRectRegionOffset, kAdjustRectRegionSize, EvidenceStatus::verified_in_ida},
        }};
    }
};

class CreateGameWindowHostView {
public:
    explicit CreateGameWindowHostView(void* base) noexcept : base_(static_cast<std::byte*>(base)) {}

    static constexpr EvidenceStatus Status() noexcept { return CreateGameWindowHostEvidence::kOffsetsStatus; }

    std::uint32_t& hinstance_x86() noexcept { return *reinterpret_cast<std::uint32_t*>(base_ + CreateGameWindowHostEvidence::kHinstanceOffset); }
    std::uint32_t& hwnd_x86() noexcept { return *reinterpret_cast<std::uint32_t*>(base_ + CreateGameWindowHostEvidence::kHwndOffset); }
    std::uint32_t& menu_x86() noexcept { return *reinterpret_cast<std::uint32_t*>(base_ + CreateGameWindowHostEvidence::kMenuOffset); }
    std::uint32_t& window_ready_flag() noexcept { return *reinterpret_cast<std::uint32_t*>(base_ + CreateGameWindowHostEvidence::kWindowReadyFlagOffset); }

    std::uint32_t hinstance_x86() const noexcept {
        std::uint32_t v = 0;
        std::memcpy(&v, base_ + CreateGameWindowHostEvidence::kHinstanceOffset, sizeof(v));
        return v;
    }

    std::uint32_t hwnd_x86() const noexcept {
        std::uint32_t v = 0;
        std::memcpy(&v, base_ + CreateGameWindowHostEvidence::kHwndOffset, sizeof(v));
        return v;
    }

    std::uint32_t menu_x86() const noexcept {
        std::uint32_t v = 0;
        std::memcpy(&v, base_ + CreateGameWindowHostEvidence::kMenuOffset, sizeof(v));
        return v;
    }

    std::uint32_t window_ready_flag() const noexcept {
        std::uint32_t v = 0;
        std::memcpy(&v, base_ + CreateGameWindowHostEvidence::kWindowReadyFlagOffset, sizeof(v));
        return v;
    }

    struct Rect32 {
        std::int32_t left;
        std::int32_t top;
        std::int32_t right;
        std::int32_t bottom;
    };

    struct Handles32 {
        std::uint32_t hinstance;
        std::uint32_t hwnd;
        std::uint32_t menu;
        std::uint32_t ready_flag;
    };

    Handles32 HandlesCopy() const noexcept {
        Handles32 h{};
        std::memcpy(&h, base_ + CreateGameWindowHostEvidence::kHandlesRegionOffset, sizeof(h));
        return h;
    }

    void SetHandles(const Handles32& h) noexcept {
        std::memcpy(base_ + CreateGameWindowHostEvidence::kHandlesRegionOffset, &h, sizeof(h));
    }

    void ClearHandles() noexcept {
        SetHandles({});
    }

    bool HasWindowHandle() const noexcept {
        return hwnd_x86() != 0;
    }

    bool HasMenuHandle() const noexcept {
        return menu_x86() != 0;
    }

    bool IsWindowReady() const noexcept {
        return window_ready_flag() != 0;
    }

    void SetWindowReady(const bool ready) noexcept {
        window_ready_flag() = ready ? 1U : 0U;
    }

    Rect32 ClientRectCopy() const noexcept {
        Rect32 r{};
        std::memcpy(&r, base_ + CreateGameWindowHostEvidence::kClientRectOffset, sizeof(r));
        return r;
    }

    Rect32 AdjustRectCopy() const noexcept {
        Rect32 r{};
        std::memcpy(&r, base_ + CreateGameWindowHostEvidence::kAdjustRectOffset, sizeof(r));
        return r;
    }

    void SetClientRect(const Rect32& r) noexcept {
        std::memcpy(base_ + CreateGameWindowHostEvidence::kClientRectOffset, &r, sizeof(r));
    }

    void SetAdjustRect(const Rect32& r) noexcept {
        std::memcpy(base_ + CreateGameWindowHostEvidence::kAdjustRectOffset, &r, sizeof(r));
    }

    std::int32_t ClientWidth() const noexcept {
        const Rect32 r = ClientRectCopy();
        return r.right - r.left;
    }

    std::int32_t ClientHeight() const noexcept {
        const Rect32 r = ClientRectCopy();
        return r.bottom - r.top;
    }

    std::int32_t AdjustedWindowWidth() const noexcept {
        const Rect32 r = AdjustRectCopy();
        return r.right - r.left;
    }

    std::int32_t AdjustedWindowHeight() const noexcept {
        const Rect32 r = AdjustRectCopy();
        return r.bottom - r.top;
    }

    std::pair<const char*, std::size_t> ClassNameBufferSpan() const noexcept {
        return {reinterpret_cast<const char*>(base_ + CreateGameWindowHostEvidence::kClassNameBufferOffset), CreateGameWindowHostEvidence::kClassNameBufferMaxCopy};
    }

private:
    std::byte* base_;
};

struct WindowClassPlan {
    static constexpr std::uint32_t kVerifiedClassStyle = 0x00002000U;
    static constexpr std::uint32_t kVerifiedWindowExStyle = 0x00000000U;

    std::string class_name;
    std::string window_title;
    int render_system_handle = 0;
    int hinstance = 0;
    int icon_resource = 0x66;
    std::uint32_t style = 0;
    std::uint32_t ex_style = 0;
    std::uint32_t class_style = 0;
    int adjusted_width = 0;
    int adjusted_height = 0;
    int client_width = 0;
    int client_height = 0;
    bool show_immediately = true;
    bool initializes_game_systems = true;
    WindowStyleKind kind = WindowStyleKind::windowed;
    EvidenceStatus style_status = EvidenceStatus::verified_in_ida;
    EvidenceStatus layout_status = EvidenceStatus::partially_verified;
    EvidenceStatus class_style_status = EvidenceStatus::verified_in_ida;
    EvidenceStatus ex_style_status = EvidenceStatus::verified_in_ida;
};

WindowClassPlan BuildWindowPlanFromIda(
    int render_system_handle,
    int hinstance,
    std::string class_name,
    std::string window_title,
    bool borderless_windowed,
    const GameConfigSnapshot& config,
    const GameStateSnapshot& state);

}  // namespace pal4

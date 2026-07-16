#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "pal4/evidence_status.h"

namespace pal4 {

struct RuntimePaths {
    std::string game_root = "I:\\PAL4\\projects\\pal4_re\\game_resources";
    std::string config_file_name = "config.cfg";
    std::string log_file_name = "pal4logfile.log";
    std::string exception_file_name = "pal4exception.log";
};

struct BootConfig {
    RuntimePaths runtime_paths;
    std::string command_line;
    int show_cmd = 10;
    bool use_original_runtime_assets = true;
};

struct CommandLineEvidence {
    std::string_view width_prefix = "width:";
    std::string_view height_prefix = "height:";
    std::string_view windowed_token = "windowed";
    EvidenceStatus width_prefix_status = EvidenceStatus::partially_verified;
    EvidenceStatus height_prefix_status = EvidenceStatus::partially_verified;
    EvidenceStatus windowed_token_status = EvidenceStatus::uncertain;
    std::string_view fullscreen_token = "fullscreen";
    EvidenceStatus fullscreen_status = EvidenceStatus::likely;
    std::string_view widescreen_token = "widescreen";
    EvidenceStatus widescreen_status = EvidenceStatus::partially_verified;
    std::string_view sync_prefix = "sync:";
    EvidenceStatus sync_status = EvidenceStatus::partially_verified;
};

struct GameConfigSnapshot {
    int width = 800;
    int height = 600;
    int bits_per_pixel = 32;
    bool quit_requested = false;
    bool fullscreen_app_requested = false;
    bool widescreen_requested = false;
};

struct GameStateSnapshot {
    int config_source = 0;
    bool fullscreen = false;
    bool widescreen = false;
    bool should_pump_messages = true;
    bool window_ready = false;
    int frame_gate = 0;
    bool sync_enabled = true;
    std::uint32_t fps_counter = 0;
    std::uint32_t previous_fps_counter = 0;
    std::uint32_t tick_now_ms = 0;
    std::uint32_t tick_reference_ms = 0;
    float minimum_frame_seconds = 0.1F;
    bool fixed_step_needs_reset = true;
    bool command_line_overrides_applied = false;
};

struct ConfigEvidenceSummary {
    std::uint32_t function_address;
    EvidenceStatus defaults_status;
    EvidenceStatus config_file_status;
    EvidenceStatus command_line_status;
};

struct RuntimeEnvironmentSnapshot {
    bool skip_ui_probe_requested = false;
    bool skip_ui_probe = false;
    std::string skip_ui_probe_raw_value;
    bool cegui_window_manager_backend_requested = false;
    bool cegui_window_manager_backend_recognized = false;
    std::string cegui_window_manager_backend_raw_value;
    std::string cegui_window_manager_backend_trimmed_value;
    std::string cegui_window_manager_backend_canonical_value;
};

GameConfigSnapshot BuildDefaultGameConfigFromIda() noexcept;
GameStateSnapshot BuildDefaultGameStateFromIda(int config_source) noexcept;
void ApplyCommandLineOverridesFromIda(
    std::string_view command_line,
    const CommandLineEvidence& evidence,
    GameConfigSnapshot& config,
    GameStateSnapshot& state);
ConfigEvidenceSummary GetConfigEvidenceSummary() noexcept;
std::string BuildRuntimePath(const RuntimePaths& paths, std::string_view file_name);
bool ShouldSkipUiProbeFromEnvValue(std::string_view raw_value) noexcept;
std::string CanonicalizeCeguiWindowManagerBackendEnvValue(std::string_view raw_value);
RuntimeEnvironmentSnapshot BuildRuntimeEnvironmentSnapshot(
    std::string_view skip_ui_probe_raw_value,
    bool has_skip_ui_probe,
    std::string_view cegui_window_manager_backend_raw_value,
    bool has_cegui_window_manager_backend) noexcept;
RuntimeEnvironmentSnapshot ReadRuntimeEnvironmentFromProcess() noexcept;

namespace config_offsets {
inline constexpr std::size_t kWidthIndex = 0;
inline constexpr std::size_t kHeightIndex = 1;
inline constexpr std::size_t kBitsPerPixelIndex = 2;
inline constexpr std::size_t kQuitFlagIndex = 3;
inline constexpr std::ptrdiff_t kStateFullscreenOffset = 8;
inline constexpr std::ptrdiff_t kStateWidescreenOffset = 0xC;
inline constexpr std::ptrdiff_t kStateSyncOffset = 0x54C;
}  // namespace config_offsets

}  // namespace pal4

#pragma once

#include <cstdint>
#include <string>

#include "pal4/cegui_resource_parser.h"
#include "pal4/cpk_repository.h"
#include "pal4/evidence_status.h"

namespace pal4 {

enum class UiRuntimeResourceBlocker : std::uint8_t {
    none = 0,
    missing_font_file,
    font_runtime_load_pending,
    font_runtime_load_failed,
    missing_imageset_image_file,
    missing_scheme_file_dependency,
    missing_scheme_font_reference,
    missing_scheme_imageset_reference,
    imageset_runtime_availability_pending,
    imageset_runtime_create_failed,
    imageset_runtime_get_failed,
    runtime_load_failed,
};

const char* ToString(UiRuntimeResourceBlocker blocker) noexcept;

struct LoadedFontResource {
    bool ok = false;
    std::string short_name;
    std::string full_path;
    bool file_present = false;
    bool parser_ready = false;
    std::string source_dependency_path;
    bool source_dependency_present = false;
    bool runtime_load_attempted = false;
    bool runtime_load_ok = false;
    bool runtime_load_failed = false;
    bool runtime_availability_checked = false;
    bool runtime_availability_pending = false;
    UiRuntimeResourceBlocker runtime_blocker = UiRuntimeResourceBlocker::none;
    ParsedCeguiFont parsed{};
};

struct LoadedImagesetResource {
    bool ok = false;
    std::string short_name;
    std::string full_path;
    bool file_present = false;
    bool parser_ready = false;
    std::string image_dependency_path;
    bool image_dependency_present = false;
    bool runtime_create_attempted = false;
    bool runtime_create_ok = false;
    bool runtime_get_attempted = false;
    bool runtime_get_ok = false;
    bool runtime_availability_checked = false;
    bool runtime_availability_pending = false;
    bool runtime_manager_ready = false;
    bool runtime_object_ready = false;
    UiRuntimeResourceBlocker runtime_blocker = UiRuntimeResourceBlocker::none;
    ParsedCeguiImageset parsed{};
};

struct LoadedSchemeResource {
    bool ok = false;
    std::string short_name;
    std::string full_path;
    std::vector<std::string> file_dependency_paths{};
    std::vector<std::string> missing_file_dependencies{};
    bool file_dependencies_ready = true;
    ParsedCeguiScheme parsed{};
};

struct LoadedLayoutResource {
    bool ok = false;
    std::string short_name;
    std::string full_path;
    std::string alias_name;
    std::string group_name;
    std::string user_hint;
    std::string requested_name;
    bool requested_name_has_explicit_suffix = false;
    struct LoadPlan {
        bool add_and_process_script = false;
        bool window_manager_load = false;
        bool cleanup_scripts = false;
        std::string script_path;
        std::string script_alias;
        std::string script_group;
    } load_plan{};
    ParsedCeguiLayout parsed{};
};

struct LoadedSequenceImageResource {
    bool ok = false;
    std::string short_name;
    std::string full_path;
    std::string image_name;
};

struct InitializeUiFrameManagerResourcesResult {
    bool ok = false;
    std::size_t font_count = 0;
    std::size_t imageset_count = 0;
    std::size_t scheme_count = 0;
    std::size_t sequence_image_count = 0;
    std::size_t loaded_font_count = 0;
    std::size_t loaded_imageset_count = 0;
    std::size_t loaded_scheme_count = 0;
    std::size_t loaded_sequence_image_count = 0;
    bool system_font_ready = false;
    bool system_font_file_present = false;
    bool system_font_parser_ready = false;
    bool oiramlook_imageset_ready = false;
    bool oiramlook_scheme_ready = false;
    bool font_file_dependencies_ready = false;
    bool system_font_runtime_ready = false;
    bool system_font_runtime_availability_checked = false;
    bool system_font_runtime_availability_pending = false;
    bool system_font_runtime_load_failed = false;
    bool imageset_image_dependencies_ready = false;
    bool oiramlook_imageset_file_present = false;
    bool oiramlook_imageset_parser_ready = false;
    bool oiramlook_imageset_image_dependency_ready = false;
    bool oiramlook_imageset_runtime_ready = false;
    bool oiramlook_imageset_runtime_availability_checked = false;
    bool oiramlook_imageset_runtime_availability_pending = false;
    bool oiramlook_imageset_runtime_manager_ready = false;
    bool oiramlook_imageset_runtime_object_ready = false;
    bool scheme_file_dependencies_ready = false;
    bool scheme_font_dependencies_ready = false;
    bool scheme_imageset_dependencies_ready = false;
    UiRuntimeResourceBlocker system_font_blocker = UiRuntimeResourceBlocker::none;
    UiRuntimeResourceBlocker oiramlook_imageset_blocker = UiRuntimeResourceBlocker::none;
    UiRuntimeResourceBlocker oiramlook_scheme_blocker = UiRuntimeResourceBlocker::none;
    LoadedFontResource system_font{};
    LoadedImagesetResource oiramlook_imageset{};
    LoadedSchemeResource oiramlook_scheme{};
    std::vector<std::string> missing_font_file_dependencies{};
    std::vector<std::string> missing_imageset_image_dependencies{};
    std::vector<std::string> missing_scheme_file_dependencies{};
    std::vector<std::string> missing_scheme_font_dependencies{};
    std::vector<std::string> missing_scheme_imageset_dependencies{};
    std::vector<std::string> oiramlook_required_files{};
    std::vector<std::string> oiramlook_required_fonts{};
    std::vector<std::string> oiramlook_required_imagesets{};
    std::vector<LoadedFontResource> loaded_fonts{};
    std::vector<LoadedImagesetResource> loaded_imagesets{};
    std::vector<LoadedSchemeResource> loaded_schemes{};
    std::vector<LoadedSequenceImageResource> loaded_sequence_images{};
};

class UiLayoutResource {
public:
    static constexpr std::uint32_t kDialogLoadLayoutAddress = 0x4BAEC0;
    static constexpr std::uint32_t kSizeX86 = 0x28;

    std::string requested_name;
    std::string requested_alias;
    std::string requested_group;
    LoadedLayoutResource loaded{};
};

EvidenceStatus UiResourceLoaderStatus() noexcept;
std::string NormalizeFontResourceName(std::string_view short_name) noexcept;
std::string NormalizeImagesetResourceName(std::string_view short_name) noexcept;
std::string NormalizeSchemeResourceName(std::string_view short_name) noexcept;
std::string NormalizeSequenceImageResourceName(std::string_view short_name) noexcept;
bool IsFontResourceName(std::string_view short_name) noexcept;
bool IsImagesetResourceName(std::string_view short_name) noexcept;
bool IsSchemeResourceName(std::string_view short_name) noexcept;
bool IsSequenceImageResourceName(std::string_view short_name) noexcept;
LoadedFontResource LoadFontFile(CpkRepository& repository, std::string_view short_name);
LoadedImagesetResource LoadImagesetFile(CpkRepository& repository, std::string_view short_name);
LoadedSchemeResource LoadSchemeFile(CpkRepository& repository, std::string_view short_name);
LoadedSequenceImageResource LoadSequenceImageFile(CpkRepository& repository, std::string_view short_name);
LoadedLayoutResource dialog_LoadLayout(
    CpkRepository& repository,
    std::string_view short_name,
    std::string_view alias_name = {},
    std::string_view user_hint = {});
void UpdateFontRuntimeAvailability(
    LoadedFontResource& resource,
    bool load_attempted,
    bool load_ok) noexcept;
void UpdateImagesetRuntimeAvailability(
    LoadedImagesetResource& resource,
    bool create_attempted,
    bool create_ok,
    bool get_attempted,
    bool get_ok) noexcept;
void FinalizeUiResourceInitialization(InitializeUiFrameManagerResourcesResult& result) noexcept;
InitializeUiFrameManagerResourcesResult InitializeUIFrameManagerResources(CpkRepository& repository);

}  // namespace pal4

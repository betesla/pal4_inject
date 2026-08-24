#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace pal4 {

struct ParsedCeguiWindowNode {
    std::string name;
    std::string type;
    std::optional<std::size_t> parent_index;
};

struct ParsedCeguiFont {
    bool valid = false;
    std::string name;
    std::string source_file;
    std::string type;
    std::vector<std::string> dependency_files;
};

struct ParsedCeguiScheme {
    bool valid = false;
    std::string name;
    std::vector<std::string> imagesets;
    std::vector<std::string> fonts;
    std::vector<std::string> looknfeels;
    std::vector<std::string> window_sets;
    std::vector<std::string> window_aliases;
    std::vector<std::string> falagard_mappings;
    std::vector<std::string> dependency_files;
};

struct ParsedCeguiImageset {
    bool valid = false;
    std::string name;
    std::string image_file;
    std::vector<std::string> image_names;
    std::vector<std::string> dependency_files;
};

struct ParsedCeguiLayout {
    bool valid = false;
    std::string root_name;
    std::string root_type;
    std::size_t window_count = 0;
    std::vector<ParsedCeguiWindowNode> windows;
    std::string new_game_window_name;
    std::string exit_window_name;
    std::vector<std::string> referenced_fonts;
    std::vector<std::string> referenced_imagesets;
    std::vector<std::string> referenced_looknfeels;
    std::vector<std::string> window_types;
    bool has_push_button = false;
    bool has_new_game_button = false;
    bool has_exit_button = false;
};

struct ParsedCeguiManagerDependencyView {
    std::vector<std::string> physical_files;
    std::vector<std::string> fonts;
    std::vector<std::string> imagesets;
    std::vector<std::string> looknfeels;
    std::vector<std::string> window_sets;
    std::vector<std::string> window_aliases;
    std::vector<std::string> falagard_mappings;
    std::vector<std::string> missing_imagesets;
};

struct ParsedCeguiSchemeImagesetRuntimeView {
    std::string scheme_name;
    std::vector<std::string> scheme_imagesets;
    std::vector<std::string> scheme_fonts;
    std::vector<std::string> scheme_looknfeels;
    std::vector<std::string> scheme_window_sets;
    std::vector<std::string> scheme_window_aliases;
    std::vector<std::string> scheme_falagard_mappings;
    std::vector<std::string> physical_files;
    std::vector<std::string> missing_imagesets;
    std::string primary_imageset_name;
    bool primary_imageset_present = false;
    std::string primary_image_file;
    std::vector<std::string> primary_image_names;
    bool static_image_mapping_present = false;
    bool multiline_edit_backdrop_present = false;
};

ParsedCeguiFont ParseCeguiFont(std::string_view xml);
ParsedCeguiScheme ParseCeguiScheme(std::string_view xml);
ParsedCeguiImageset ParseCeguiImageset(std::string_view xml);
ParsedCeguiLayout ParseCeguiLayout(std::string_view xml);
ParsedCeguiManagerDependencyView BuildImagesetDependencyView(const ParsedCeguiImageset& imageset);
ParsedCeguiManagerDependencyView BuildSchemeDependencyView(
    const ParsedCeguiScheme& scheme,
    const std::vector<ParsedCeguiImageset>& parsed_imagesets);
ParsedCeguiSchemeImagesetRuntimeView BuildSchemeImagesetRuntimeView(
    const ParsedCeguiScheme& scheme,
    const std::vector<ParsedCeguiImageset>& parsed_imagesets,
    std::string_view preferred_imageset = {});

}  // namespace pal4

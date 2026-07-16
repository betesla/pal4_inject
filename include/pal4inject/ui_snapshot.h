#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pal4::inject {

struct UiRect {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
};

struct UiSnapshotNode {
    std::string ref;
    std::string type;
    std::string name;
    std::string path;
    std::string text;
    UiRect rect{};
    bool visible = false;
    bool enabled = false;
    bool focused = false;
    bool editable = false;
    bool clickable = false;
    std::vector<UiSnapshotNode> children;
};

struct UiSnapshotTree {
    UiSnapshotNode root;
};

std::string SerializeUiSnapshotTree(const UiSnapshotTree& tree);
bool ParseUiSnapshotTree(
    std::string_view text,
    UiSnapshotTree* out,
    std::string* error);
std::size_t CountUiSnapshotNodes(const UiSnapshotTree& tree) noexcept;
const UiSnapshotNode* FindUiSnapshotNodeByRef(
    const UiSnapshotTree& tree,
    std::string_view ref) noexcept;
const UiSnapshotNode* FindUiSnapshotNodeByPath(
    const UiSnapshotTree& tree,
    std::string_view path) noexcept;
// Resolves a unique complete path first, then a unique slash-delimited suffix.
// Returns null for missing or ambiguous suffixes so automation never clicks a
// similarly named control by accident.
const UiSnapshotNode* FindUniqueUiSnapshotNodeByPathSuffix(
    const UiSnapshotTree& tree,
    std::string_view path_or_suffix) noexcept;
bool UiSnapshotTreeContainsText(
    const UiSnapshotTree& tree,
    std::string_view text_substring) noexcept;
std::string FormatUiSnapshotTreeForDisplay(const UiSnapshotTree& tree);

}  // namespace pal4::inject

#include "pal4inject/ui_snapshot.h"

#include <vector>

#include "pal4inject/protocol.h"

namespace pal4::inject {
namespace {

struct FlatUiSnapshotNode {
    std::size_t depth = 0;
    UiSnapshotNode node{};
};

void FlattenUiSnapshotNode(
    const UiSnapshotNode& node,
    const std::size_t depth,
    std::vector<FlatUiSnapshotNode>* out) {
    out->push_back({depth, node});
    for (const auto& child : node.children) {
        FlattenUiSnapshotNode(child, depth + 1, out);
    }
}

void AppendSerializedNode(
    const FlatUiSnapshotNode& flat,
    std::string* out) {
    if (!out->empty()) {
        out->push_back('\n');
    }

    const auto append_field =
        [out](const std::string_view key, const std::string_view value) {
            if (!out->empty() && out->back() != '\n') {
                out->push_back(',');
            }
            *out += std::string(key);
            out->push_back('=');
            *out += EscapeProtocolToken(value);
        };

    append_field("depth", std::to_string(flat.depth));
    append_field("ref", flat.node.ref);
    append_field("type", flat.node.type);
    append_field("name", flat.node.name);
    append_field("path", flat.node.path);
    append_field("text", flat.node.text);
    append_field("left", std::to_string(flat.node.rect.left));
    append_field("top", std::to_string(flat.node.rect.top));
    append_field("right", std::to_string(flat.node.rect.right));
    append_field("bottom", std::to_string(flat.node.rect.bottom));
    append_field("visible", flat.node.visible ? "1" : "0");
    append_field("enabled", flat.node.enabled ? "1" : "0");
    append_field("focused", flat.node.focused ? "1" : "0");
    append_field("editable", flat.node.editable ? "1" : "0");
    append_field("clickable", flat.node.clickable ? "1" : "0");
}

bool ParseBoolField(
    const std::string_view value,
    bool* out) {
    if (!out) {
        return false;
    }
    if (value == "1" || value == "true") {
        *out = true;
        return true;
    }
    if (value == "0" || value == "false") {
        *out = false;
        return true;
    }
    return false;
}

bool ParseFlatUiSnapshotNode(
    const std::string_view line,
    FlatUiSnapshotNode* out,
    std::string* error) {
    if (!out) {
        if (error) {
            *error = "flat ui snapshot output pointer is null";
        }
        return false;
    }

    FlatUiSnapshotNode parsed{};
    std::size_t cursor = 0;
    while (cursor < line.size()) {
        const std::size_t next = line.find(',', cursor);
        const std::string_view token = next == std::string_view::npos
            ? line.substr(cursor)
            : line.substr(cursor, next - cursor);
        if (token.empty()) {
            if (next == std::string_view::npos) {
                break;
            }
            cursor = next + 1;
            continue;
        }
        const std::size_t pivot = token.find('=');
        if (pivot == std::string_view::npos) {
            if (error) {
                *error = "malformed ui snapshot token: " + std::string(token);
            }
            return false;
        }

        const std::string key(token.substr(0, pivot));
        const std::string value = UnescapeProtocolToken(token.substr(pivot + 1));
        if (key == "depth") {
            parsed.depth = static_cast<std::size_t>(std::stoul(value));
        } else if (key == "ref") {
            parsed.node.ref = value;
        } else if (key == "type") {
            parsed.node.type = value;
        } else if (key == "name") {
            parsed.node.name = value;
        } else if (key == "path") {
            parsed.node.path = value;
        } else if (key == "text") {
            parsed.node.text = value;
        } else if (key == "left") {
            parsed.node.rect.left = std::stoi(value);
        } else if (key == "top") {
            parsed.node.rect.top = std::stoi(value);
        } else if (key == "right") {
            parsed.node.rect.right = std::stoi(value);
        } else if (key == "bottom") {
            parsed.node.rect.bottom = std::stoi(value);
        } else if (key == "visible") {
            if (!ParseBoolField(value, &parsed.node.visible)) {
                if (error) {
                    *error = "invalid visible flag";
                }
                return false;
            }
        } else if (key == "enabled") {
            if (!ParseBoolField(value, &parsed.node.enabled)) {
                if (error) {
                    *error = "invalid enabled flag";
                }
                return false;
            }
        } else if (key == "focused") {
            if (!ParseBoolField(value, &parsed.node.focused)) {
                if (error) {
                    *error = "invalid focused flag";
                }
                return false;
            }
        } else if (key == "editable") {
            if (!ParseBoolField(value, &parsed.node.editable)) {
                if (error) {
                    *error = "invalid editable flag";
                }
                return false;
            }
        } else if (key == "clickable") {
            if (!ParseBoolField(value, &parsed.node.clickable)) {
                if (error) {
                    *error = "invalid clickable flag";
                }
                return false;
            }
        }

        if (next == std::string_view::npos) {
            break;
        }
        cursor = next + 1;
    }

    *out = parsed;
    return true;
}

const UiSnapshotNode* FindUiSnapshotNodeByRefRecursive(
    const UiSnapshotNode& node,
    const std::string_view ref) noexcept {
    if (node.ref == ref) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const auto* found = FindUiSnapshotNodeByRefRecursive(child, ref)) {
            return found;
        }
    }
    return nullptr;
}

const UiSnapshotNode* FindUiSnapshotNodeByPathRecursive(
    const UiSnapshotNode& node,
    const std::string_view path) noexcept {
    if (node.path == path) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const auto* found = FindUiSnapshotNodeByPathRecursive(child, path)) {
            return found;
        }
    }
    return nullptr;
}

bool UiSnapshotTreeContainsTextRecursive(
    const UiSnapshotNode& node,
    const std::string_view text_substring) noexcept {
    if (node.text.find(text_substring) != std::string::npos) {
        return true;
    }
    for (const auto& child : node.children) {
        if (UiSnapshotTreeContainsTextRecursive(child, text_substring)) {
            return true;
        }
    }
    return false;
}

void FormatUiSnapshotNodeForDisplay(
    const UiSnapshotNode& node,
    const std::size_t depth,
    std::string* out) {
    out->append(depth * 2, ' ');
    *out += "- ";
    *out += node.type.empty() ? "window" : node.type;
    if (!node.text.empty()) {
        *out += " \"" + node.text + "\"";
    } else if (!node.name.empty()) {
        *out += " \"" + node.name + "\"";
    }
    if (!node.ref.empty()) {
        *out += " [ref=" + node.ref + "]";
    }
    if (!node.name.empty() && node.text != node.name) {
        *out += " [name=" + node.name + "]";
    }
    if (!node.path.empty()) {
        *out += " [path=" + node.path + "]";
    }
    *out += '\n';
    for (const auto& child : node.children) {
        FormatUiSnapshotNodeForDisplay(child, depth + 1, out);
    }
}

std::size_t CountUiSnapshotNodesRecursive(const UiSnapshotNode& node) noexcept {
    std::size_t count = 1;
    for (const auto& child : node.children) {
        count += CountUiSnapshotNodesRecursive(child);
    }
    return count;
}

}  // namespace

std::string SerializeUiSnapshotTree(const UiSnapshotTree& tree) {
    std::vector<FlatUiSnapshotNode> flat_nodes;
    FlattenUiSnapshotNode(tree.root, 0, &flat_nodes);

    std::string out;
    for (const auto& flat : flat_nodes) {
        AppendSerializedNode(flat, &out);
    }
    return out;
}

bool ParseUiSnapshotTree(
    const std::string_view text,
    UiSnapshotTree* out,
    std::string* error) {
    if (!out) {
        if (error) {
            *error = "ui snapshot output pointer is null";
        }
        return false;
    }
    if (text.empty()) {
        if (error) {
            *error = "ui snapshot payload is empty";
        }
        return false;
    }

    std::vector<FlatUiSnapshotNode> flat_nodes;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const std::size_t next = text.find('\n', cursor);
        const std::string_view line = next == std::string_view::npos
            ? text.substr(cursor)
            : text.substr(cursor, next - cursor);
        if (!line.empty()) {
            FlatUiSnapshotNode flat{};
            if (!ParseFlatUiSnapshotNode(line, &flat, error)) {
                if (error && !error->empty()) {
                    *error += " | line=" + std::string(line);
                }
                return false;
            }
            flat_nodes.push_back(std::move(flat));
        }
        if (next == std::string_view::npos) {
            break;
        }
        cursor = next + 1;
    }

    if (flat_nodes.empty()) {
        if (error) {
            *error = "ui snapshot tree has no nodes";
        }
        return false;
    }
    if (flat_nodes.front().depth != 0) {
        if (error) {
            *error = "ui snapshot root depth must be 0";
        }
        return false;
    }

    UiSnapshotTree parsed{};
    parsed.root = std::move(flat_nodes.front().node);
    std::vector<UiSnapshotNode*> stack;
    stack.push_back(&parsed.root);

    for (std::size_t i = 1; i < flat_nodes.size(); ++i) {
        auto flat = std::move(flat_nodes[i]);
        if (flat.depth == 0 || flat.depth > stack.size()) {
            if (error) {
                *error = "invalid ui snapshot tree depth";
            }
            return false;
        }
        stack.resize(flat.depth);
        auto* parent = stack.back();
        parent->children.push_back(std::move(flat.node));
        stack.push_back(&parent->children.back());
    }

    *out = std::move(parsed);
    return true;
}

std::size_t CountUiSnapshotNodes(const UiSnapshotTree& tree) noexcept {
    return CountUiSnapshotNodesRecursive(tree.root);
}

const UiSnapshotNode* FindUiSnapshotNodeByRef(
    const UiSnapshotTree& tree,
    const std::string_view ref) noexcept {
    return FindUiSnapshotNodeByRefRecursive(tree.root, ref);
}

const UiSnapshotNode* FindUiSnapshotNodeByPath(
    const UiSnapshotTree& tree,
    const std::string_view path) noexcept {
    return FindUiSnapshotNodeByPathRecursive(tree.root, path);
}

bool UiSnapshotTreeContainsText(
    const UiSnapshotTree& tree,
    const std::string_view text_substring) noexcept {
    return UiSnapshotTreeContainsTextRecursive(tree.root, text_substring);
}

std::string FormatUiSnapshotTreeForDisplay(const UiSnapshotTree& tree) {
    std::string out;
    FormatUiSnapshotNodeForDisplay(tree.root, 0, &out);
    if (!out.empty() && out.back() == '\n') {
        out.pop_back();
    }
    return out;
}

}  // namespace pal4::inject

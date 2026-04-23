#include "cegui_font_texture_registry.h"

#include <array>
#include <mutex>
#include <string>
#include <unordered_set>

#include "cegui_bindings.h"
#include "pal4inject/cegui_font_resync.h"

namespace pal4::inject {
namespace {

std::mutex g_known_font_texture_mutex;
std::unordered_set<const void*> g_known_font_textures;
std::unordered_set<std::string> g_registered_font_names;

constexpr std::array<std::string_view, 3> kKnownDynamicUiFonts{
    "system",
    "systemBold",
    "dialog_simsun",
};

struct ScopedCeguiString {
    const CeguiBindings* bindings = nullptr;
    OpaqueCeguiString storage{};
    bool constructed = false;

    ~ScopedCeguiString() {
        if (constructed && bindings && bindings->cegui_string_dtor) {
            bindings->cegui_string_dtor(&storage);
        }
    }
};

bool BuildCeguiAnsiString(
    const CeguiBindings& bindings,
    const std::string_view text,
    ScopedCeguiString* out,
    std::string* error) {
    if (!out) {
        if (error) {
            *error = "CEGUI string output is null";
        }
        return false;
    }
    if (!bindings.cegui_string_ctor_from_ansi || !bindings.cegui_string_dtor) {
        if (error) {
            *error = "CEGUI string helpers are unavailable";
        }
        return false;
    }

    out->bindings = &bindings;
    const std::string ansi(text);
    bindings.cegui_string_ctor_from_ansi(&out->storage, ansi.c_str());
    out->constructed = true;
    return true;
}

}  // namespace

void RememberKnownDynamicFontTexture(
    const std::string_view font_name,
    const void* texture) noexcept {
    if (!texture) {
        return;
    }
    std::scoped_lock lock(g_known_font_texture_mutex);
    g_known_font_textures.insert(texture);
    if (!font_name.empty()) {
        g_registered_font_names.insert(std::string(font_name));
    }
}

bool IsKnownDynamicFontTexture(const void* texture) noexcept {
    if (!texture) {
        return false;
    }
    std::scoped_lock lock(g_known_font_texture_mutex);
    return g_known_font_textures.find(texture) != g_known_font_textures.end();
}

bool RefreshKnownDynamicFontTextures(std::string* error) {
    CeguiBindings bindings{};
    if (!TryGetCeguiBindings(&bindings, error)) {
        return false;
    }
    if (!bindings.get_imageset_manager_singleton_ptr ||
        !bindings.imageset_manager_is_imageset_present ||
        !bindings.imageset_manager_get_imageset ||
        !bindings.imageset_get_texture) {
        if (error) {
            *error = "imageset bindings are unavailable";
        }
        return false;
    }

    void* imageset_manager = bindings.get_imageset_manager_singleton_ptr();
    if (!imageset_manager) {
        if (error) {
            *error = "CEGUI ImagesetManager singleton is null";
        }
        return false;
    }

    bool found_any = false;
    for (const auto font_name : kKnownDynamicUiFonts) {
        {
            std::scoped_lock lock(g_known_font_texture_mutex);
            if (g_registered_font_names.find(std::string(font_name)) !=
                g_registered_font_names.end()) {
                continue;
            }
        }

        const std::string atlas_name = BuildKnownDynamicUiFontAtlasName(font_name);
        if (atlas_name.empty()) {
            continue;
        }

        ScopedCeguiString atlas_name_string{};
        if (!BuildCeguiAnsiString(bindings, atlas_name, &atlas_name_string, error)) {
            return false;
        }

        if (!bindings.imageset_manager_is_imageset_present(
                imageset_manager,
                &atlas_name_string.storage)) {
            continue;
        }

        void* imageset = bindings.imageset_manager_get_imageset(
            imageset_manager,
            &atlas_name_string.storage);
        if (!imageset) {
            continue;
        }

        void* texture = bindings.imageset_get_texture(imageset);
        if (!texture) {
            continue;
        }

        RememberKnownDynamicFontTexture(font_name, texture);
        found_any = true;
    }

    if (error) {
        error->clear();
    }
    return found_any;
}

}  // namespace pal4::inject

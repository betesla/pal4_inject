#include "cegui_font_experiment.h"

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "cegui_bindings.h"
#include "hook_logging.h"
#include "pal4inject/cegui_font_experiment.h"

namespace pal4::inject {
namespace {

constexpr std::size_t kFontHeightIndex = 51;
constexpr std::size_t kLineSpacingIndex = 52;
constexpr std::size_t kBaselineIndex = 53;

struct FontVerticalMetrics {
    float font_height = 0.0F;
    float line_spacing = 0.0F;
    float baseline = 0.0F;
};

using FontDrawTextFn = int (__thiscall*)(
    void*,
    void*,
    float*,
    float,
    void*,
    int,
    void*,
    float,
    float);
using FontGetTextExtentFn = double (__thiscall*)(void*, const void*, float);
using FontGetCharAtPixelFn = unsigned int (__thiscall*)(void*, const void*, unsigned int, float, float);
using FontGetFormattedTextExtentFn = double (__thiscall*)(void*, void*, void*, int, float);
using FontGetWrappedTextExtentFn = float (__thiscall*)(void*, const void*, float, float);
using ImagesetDefineImageFn = void (__thiscall*)(void*, const void*, const void*, const void*);
using TextGlyphCacheGlyphFn = void (__thiscall*)(void*, const void*, float, void*, const void*, const void*);

constexpr char kDrawTextSymbol[] =
    "?drawText@Font@CEGUI@@QBEIABVString@2@ABVRect@2@M1W4TextFormatting@2@ABVColourRect@2@MM@Z";
constexpr std::array<std::uint8_t, 3> kDrawTextExpectedPrefix{0x6A, 0xFF, 0x68};
constexpr std::size_t kDrawTextPatchSpan = 7;
constexpr char kGetTextExtentSymbol[] =
    "?getTextExtent@Font@CEGUI@@QBEMABVString@2@M@Z";
constexpr std::array<std::uint8_t, 4> kGetTextExtentExpectedPrefix{0x83, 0xEC, 0x24, 0x53};
constexpr std::size_t kGetTextExtentPatchSpan = 10;
constexpr char kGetCharAtPixelSymbol[] =
    "?getCharAtPixel@Font@CEGUI@@QBEIABVString@2@IMM@Z";
constexpr std::array<std::uint8_t, 4> kGetCharAtPixelExpectedPrefix{0x83, 0xEC, 0x14, 0xD9};
constexpr std::size_t kGetCharAtPixelPatchSpan = 14;
constexpr char kGetFormattedTextExtentSymbol[] =
    "?getFormattedTextExtent@Font@CEGUI@@QBEMABVString@2@ABVRect@2@W4TextFormatting@2@M@Z";
constexpr std::array<std::uint8_t, 3> kGetFormattedTextExtentExpectedPrefix{0x6A, 0xFF, 0x68};
constexpr std::size_t kGetFormattedTextExtentPatchSpan = 7;
constexpr char kGetWrappedTextExtentSymbol[] =
    "?getWrappedTextExtent@Font@CEGUI@@ABEMABVString@2@MM@Z";
constexpr std::array<std::uint8_t, 3> kGetWrappedTextExtentExpectedPrefix{0x6A, 0xFF, 0x68};
constexpr std::size_t kGetWrappedTextExtentPatchSpan = 7;
constexpr char kDefineImageSymbol[] =
    "?defineImage@Imageset@CEGUI@@QAEXABVString@2@ABVRect@2@ABVVector2@2@@Z";
constexpr std::array<std::uint8_t, 27> kDefineImageExpectedPrefix{
    0x64, 0xA1, 0x00, 0x00, 0x00, 0x00,
    0x6A, 0xFF,
    0x68, 0x33, 0xC1, 0x14, 0x10,
    0x50,
    0x64, 0x89, 0x25, 0x00, 0x00, 0x00, 0x00,
    0x81, 0xEC, 0xEC, 0x05, 0x00, 0x00};
constexpr std::size_t kDefineImagePatchSpan = kDefineImageExpectedPrefix.size();
constexpr char kOiramlookModuleName[] = "OIRAMLOOK.dll";
constexpr char kTextGlyphGetHeightSymbol[] =
    "?getHeight@TextGlyph@RichText@CEGUI@@UBEMXZ";
constexpr std::array<std::uint8_t, 13> kTextGlyphGetHeightExpectedBytes{
    0x8B, 0x81, 0xB4, 0x00, 0x00, 0x00,
    0xD9, 0x80, 0xD0, 0x00, 0x00, 0x00,
    0xC3};
constexpr std::size_t kTextGlyphGetHeightPatchSpan = kTextGlyphGetHeightExpectedBytes.size();
constexpr char kTextGlyphCacheGlyphSymbol[] =
    "?cacheGlyph@TextGlyph@RichText@CEGUI@@UAEXABVRect@3@MAAVRenderCache@3@PBVString@3@PBV43@@Z";
constexpr std::array<std::uint8_t, 7> kTextGlyphCacheGlyphExpectedPrefix{
    0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x56};
constexpr std::size_t kTextGlyphCacheGlyphPatchSpan = 7;
constexpr std::ptrdiff_t kTextGlyphFontOffset = 0xB4;
constexpr std::size_t kMaxLoggedTextGlyphHeightCalls = 16;
constexpr std::size_t kMaxLoggedTextGlyphCacheCalls = 12;

std::mutex g_mutex;
std::mutex g_text_glyph_height_hook_mutex;
std::atomic<unsigned int> g_logged_get_char_at_pixel_calls{0};
std::atomic<unsigned int> g_logged_text_glyph_height_calls{0};
std::atomic<unsigned int> g_logged_text_glyph_cache_calls{0};
std::atomic<unsigned int> g_logged_define_image_adjustments{0};
FontDrawTextFn g_original_font_draw_text = nullptr;
FontGetTextExtentFn g_original_font_get_text_extent = nullptr;
FontGetCharAtPixelFn g_original_font_get_char_at_pixel = nullptr;
FontGetFormattedTextExtentFn g_original_font_get_formatted_text_extent = nullptr;
FontGetWrappedTextExtentFn g_original_font_get_wrapped_text_extent = nullptr;
ImagesetDefineImageFn g_original_imageset_define_image = nullptr;
TextGlyphCacheGlyphFn g_original_text_glyph_cache_glyph = nullptr;
bool g_draw_text_hook_installed = false;
bool g_get_text_extent_hook_installed = false;
bool g_get_char_at_pixel_hook_installed = false;
bool g_get_formatted_text_extent_hook_installed = false;
bool g_get_wrapped_text_extent_hook_installed = false;
bool g_define_image_hook_installed = false;
bool g_text_glyph_get_height_hook_installed = false;
bool g_text_glyph_cache_glyph_hook_installed = false;
std::unordered_map<void*, DynamicFontOversamplePlan> g_oversampled_fonts;
thread_local void* g_compensated_draw_text_font = nullptr;
thread_local unsigned int g_compensated_draw_text_depth = 0;
thread_local std::string g_active_define_image_font_name;
thread_local float g_active_define_image_offset_y = 0.0F;

struct GlyphImageOffsetScope {
    explicit GlyphImageOffsetScope(std::string font_name, const float offset_y)
        : previous_font_name_(std::move(g_active_define_image_font_name)),
          previous_offset_y_(g_active_define_image_offset_y) {
        g_active_define_image_font_name = std::move(font_name);
        g_active_define_image_offset_y = offset_y;
    }

    ~GlyphImageOffsetScope() {
        g_active_define_image_font_name = std::move(previous_font_name_);
        g_active_define_image_offset_y = previous_offset_y_;
    }

    std::string previous_font_name_;
    float previous_offset_y_ = 0.0F;
};

struct CeguiVector2Value {
    float x = 0.0F;
    float y = 0.0F;
};

int __fastcall HookedFontDrawText(
    void* font,
    void*,
    void* text,
    float* rect,
    float z,
    void* clipper,
    int formatting,
    void* colour_rect,
    float x_scale,
    float y_scale);

double __fastcall HookedFontGetTextExtent(
    void* font,
    void*,
    const void* text,
    float x_scale);

unsigned int __fastcall HookedFontGetCharAtPixel(
    void* font,
    void*,
    const void* text,
    unsigned int start_char,
    float pixel,
    float x_scale);

double __fastcall HookedFontGetFormattedTextExtent(
    void* font,
    void*,
    void* text,
    void* rect,
    int formatting,
    float x_scale);

float __fastcall HookedFontGetWrappedTextExtent(
    void* font,
    void*,
    const void* text,
    float wrap_width,
    float x_scale);

void __fastcall HookedImagesetDefineImage(
    void* imageset,
    void*,
    const void* name,
    const void* source_rect,
    const void* offset);

float __fastcall HookedTextGlyphGetHeight(
    void* glyph,
    void*);

void __fastcall HookedTextGlyphCacheGlyph(
    void* glyph,
    void*,
    const void* rect,
    float z,
    void* render_cache,
    const void* text,
    const void* clip_rect);

bool WriteRelativeJump(
    void* target,
    void* destination,
    const std::size_t patch_span,
    std::string* error) {
    if (patch_span < 5) {
        if (error) {
            *error = "patch span must be at least 5 bytes";
        }
        return false;
    }

    const auto delta = reinterpret_cast<std::intptr_t>(destination) -
        (reinterpret_cast<std::intptr_t>(target) + 5);
    if (delta < INT32_MIN || delta > INT32_MAX) {
        if (error) {
            *error = "detour destination is out of 32-bit relative jump range";
        }
        return false;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(target, patch_span, PAGE_EXECUTE_READWRITE, &old_protect)) {
        if (error) {
            *error = "VirtualProtect failed while patching drawText";
        }
        return false;
    }

    auto* patch = static_cast<std::uint8_t*>(target);
    patch[0] = 0xE9;
    *reinterpret_cast<std::int32_t*>(patch + 1) = static_cast<std::int32_t>(delta);
    for (std::size_t i = 5; i < patch_span; ++i) {
        patch[i] = 0x90;
    }
    FlushInstructionCache(GetCurrentProcess(), target, patch_span);

    DWORD discard = 0;
    VirtualProtect(target, patch_span, old_protect, &discard);
    return true;
}

bool InstallKnownSafeDrawTextHook(std::string* error) {
    if (g_draw_text_hook_installed) {
        return true;
    }

    const HMODULE module = GetModuleHandleA("CEGUIBase.dll");
    if (!module) {
        if (error) {
            *error = "CEGUIBase.dll is not loaded";
        }
        return false;
    }

    auto* target = reinterpret_cast<std::uint8_t*>(GetProcAddress(module, kDrawTextSymbol));
    if (!target) {
        if (error) {
            *error = "GetProcAddress failed for CEGUI Font::drawText";
        }
        return false;
    }

    if (std::memcmp(target, kDrawTextExpectedPrefix.data(), kDrawTextExpectedPrefix.size()) != 0) {
        if (error) {
            *error = "unexpected Font::drawText prologue";
        }
        return false;
    }

    void* trampoline = VirtualAlloc(
        nullptr,
        kDrawTextPatchSpan + 5,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!trampoline) {
        if (error) {
            *error = "VirtualAlloc failed for Font::drawText trampoline";
        }
        return false;
    }

    std::memcpy(trampoline, target, kDrawTextPatchSpan);
    void* continue_at = target + kDrawTextPatchSpan;
    if (!WriteRelativeJump(
            static_cast<std::uint8_t*>(trampoline) + kDrawTextPatchSpan,
            continue_at,
            5,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    if (!WriteRelativeJump(
            target,
            reinterpret_cast<void*>(&HookedFontDrawText),
            kDrawTextPatchSpan,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    g_original_font_draw_text = reinterpret_cast<FontDrawTextFn>(trampoline);
    g_draw_text_hook_installed = true;
    return true;
}

bool InstallKnownSafeGetTextExtentHook(std::string* error) {
    if (g_get_text_extent_hook_installed) {
        return true;
    }

    const HMODULE module = GetModuleHandleA("CEGUIBase.dll");
    if (!module) {
        if (error) {
            *error = "CEGUIBase.dll is not loaded";
        }
        return false;
    }

    auto* target = reinterpret_cast<std::uint8_t*>(GetProcAddress(module, kGetTextExtentSymbol));
    if (!target) {
        if (error) {
            *error = "GetProcAddress failed for CEGUI Font::getTextExtent";
        }
        return false;
    }

    if (std::memcmp(
            target,
            kGetTextExtentExpectedPrefix.data(),
            kGetTextExtentExpectedPrefix.size()) != 0) {
        if (error) {
            *error = "unexpected Font::getTextExtent prologue";
        }
        return false;
    }

    void* trampoline = VirtualAlloc(
        nullptr,
        kGetTextExtentPatchSpan + 5,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!trampoline) {
        if (error) {
            *error = "VirtualAlloc failed for Font::getTextExtent trampoline";
        }
        return false;
    }

    std::memcpy(trampoline, target, kGetTextExtentPatchSpan);
    void* continue_at = target + kGetTextExtentPatchSpan;
    if (!WriteRelativeJump(
            static_cast<std::uint8_t*>(trampoline) + kGetTextExtentPatchSpan,
            continue_at,
            5,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    if (!WriteRelativeJump(
            target,
            reinterpret_cast<void*>(&HookedFontGetTextExtent),
            kGetTextExtentPatchSpan,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    g_original_font_get_text_extent = reinterpret_cast<FontGetTextExtentFn>(trampoline);
    g_get_text_extent_hook_installed = true;
    return true;
}

bool InstallKnownSafeGetCharAtPixelHook(std::string* error) {
    if (g_get_char_at_pixel_hook_installed) {
        return true;
    }

    const HMODULE module = GetModuleHandleA("CEGUIBase.dll");
    if (!module) {
        if (error) {
            *error = "CEGUIBase.dll is not loaded";
        }
        return false;
    }

    auto* target = reinterpret_cast<std::uint8_t*>(GetProcAddress(module, kGetCharAtPixelSymbol));
    if (!target) {
        if (error) {
            *error = "GetProcAddress failed for CEGUI Font::getCharAtPixel";
        }
        return false;
    }

    if (std::memcmp(
            target,
            kGetCharAtPixelExpectedPrefix.data(),
            kGetCharAtPixelExpectedPrefix.size()) != 0) {
        if (error) {
            *error = "unexpected Font::getCharAtPixel prologue";
        }
        return false;
    }

    void* trampoline = VirtualAlloc(
        nullptr,
        kGetCharAtPixelPatchSpan + 5,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!trampoline) {
        if (error) {
            *error = "VirtualAlloc failed for Font::getCharAtPixel trampoline";
        }
        return false;
    }

    std::memcpy(trampoline, target, kGetCharAtPixelPatchSpan);
    void* continue_at = target + kGetCharAtPixelPatchSpan;
    if (!WriteRelativeJump(
            static_cast<std::uint8_t*>(trampoline) + kGetCharAtPixelPatchSpan,
            continue_at,
            5,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    if (!WriteRelativeJump(
            target,
            reinterpret_cast<void*>(&HookedFontGetCharAtPixel),
            kGetCharAtPixelPatchSpan,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    g_original_font_get_char_at_pixel = reinterpret_cast<FontGetCharAtPixelFn>(trampoline);
    g_get_char_at_pixel_hook_installed = true;
    return true;
}

bool InstallKnownSafeGetFormattedTextExtentHook(std::string* error) {
    if (g_get_formatted_text_extent_hook_installed) {
        return true;
    }

    const HMODULE module = GetModuleHandleA("CEGUIBase.dll");
    if (!module) {
        if (error) {
            *error = "CEGUIBase.dll is not loaded";
        }
        return false;
    }

    auto* target =
        reinterpret_cast<std::uint8_t*>(GetProcAddress(module, kGetFormattedTextExtentSymbol));
    if (!target) {
        if (error) {
            *error = "GetProcAddress failed for CEGUI Font::getFormattedTextExtent";
        }
        return false;
    }

    if (std::memcmp(
            target,
            kGetFormattedTextExtentExpectedPrefix.data(),
            kGetFormattedTextExtentExpectedPrefix.size()) != 0) {
        if (error) {
            *error = "unexpected Font::getFormattedTextExtent prologue";
        }
        return false;
    }

    void* trampoline = VirtualAlloc(
        nullptr,
        kGetFormattedTextExtentPatchSpan + 5,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!trampoline) {
        if (error) {
            *error = "VirtualAlloc failed for Font::getFormattedTextExtent trampoline";
        }
        return false;
    }

    std::memcpy(trampoline, target, kGetFormattedTextExtentPatchSpan);
    void* continue_at = target + kGetFormattedTextExtentPatchSpan;
    if (!WriteRelativeJump(
            static_cast<std::uint8_t*>(trampoline) + kGetFormattedTextExtentPatchSpan,
            continue_at,
            5,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    if (!WriteRelativeJump(
            target,
            reinterpret_cast<void*>(&HookedFontGetFormattedTextExtent),
            kGetFormattedTextExtentPatchSpan,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    g_original_font_get_formatted_text_extent =
        reinterpret_cast<FontGetFormattedTextExtentFn>(trampoline);
    g_get_formatted_text_extent_hook_installed = true;
    return true;
}

bool InstallKnownSafeGetWrappedTextExtentHook(std::string* error) {
    if (g_get_wrapped_text_extent_hook_installed) {
        return true;
    }

    const HMODULE module = GetModuleHandleA("CEGUIBase.dll");
    if (!module) {
        if (error) {
            *error = "CEGUIBase.dll is not loaded";
        }
        return false;
    }

    auto* target =
        reinterpret_cast<std::uint8_t*>(GetProcAddress(module, kGetWrappedTextExtentSymbol));
    if (!target) {
        if (error) {
            *error = "GetProcAddress failed for CEGUI Font::getWrappedTextExtent";
        }
        return false;
    }

    if (std::memcmp(
            target,
            kGetWrappedTextExtentExpectedPrefix.data(),
            kGetWrappedTextExtentExpectedPrefix.size()) != 0) {
        if (error) {
            *error = "unexpected Font::getWrappedTextExtent prologue";
        }
        return false;
    }

    void* trampoline = VirtualAlloc(
        nullptr,
        kGetWrappedTextExtentPatchSpan + 5,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!trampoline) {
        if (error) {
            *error = "VirtualAlloc failed for Font::getWrappedTextExtent trampoline";
        }
        return false;
    }

    std::memcpy(trampoline, target, kGetWrappedTextExtentPatchSpan);
    void* continue_at = target + kGetWrappedTextExtentPatchSpan;
    if (!WriteRelativeJump(
            static_cast<std::uint8_t*>(trampoline) + kGetWrappedTextExtentPatchSpan,
            continue_at,
            5,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    if (!WriteRelativeJump(
            target,
            reinterpret_cast<void*>(&HookedFontGetWrappedTextExtent),
            kGetWrappedTextExtentPatchSpan,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    g_original_font_get_wrapped_text_extent =
        reinterpret_cast<FontGetWrappedTextExtentFn>(trampoline);
    g_get_wrapped_text_extent_hook_installed = true;
    return true;
}

bool InstallKnownSafeDefineImageHook(std::string* error) {
    if (g_define_image_hook_installed) {
        return true;
    }

    const HMODULE module = GetModuleHandleA("CEGUIBase.dll");
    if (!module) {
        if (error) {
            *error = "CEGUIBase.dll is not loaded";
        }
        return false;
    }

    auto* target = reinterpret_cast<std::uint8_t*>(GetProcAddress(module, kDefineImageSymbol));
    if (!target) {
        if (error) {
            *error = "GetProcAddress failed for Imageset::defineImage";
        }
        return false;
    }

    if (std::memcmp(
            target,
            kDefineImageExpectedPrefix.data(),
            kDefineImageExpectedPrefix.size()) != 0) {
        if (error) {
            *error = "unexpected Imageset::defineImage prologue";
        }
        return false;
    }

    void* trampoline = VirtualAlloc(
        nullptr,
        kDefineImagePatchSpan + 5,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!trampoline) {
        if (error) {
            *error = "VirtualAlloc failed for Imageset::defineImage trampoline";
        }
        return false;
    }

    std::memcpy(trampoline, target, kDefineImagePatchSpan);
    void* continue_at = target + kDefineImagePatchSpan;
    if (!WriteRelativeJump(
            static_cast<std::uint8_t*>(trampoline) + kDefineImagePatchSpan,
            continue_at,
            5,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    if (!WriteRelativeJump(
            target,
            reinterpret_cast<void*>(&HookedImagesetDefineImage),
            kDefineImagePatchSpan,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    g_original_imageset_define_image =
        reinterpret_cast<ImagesetDefineImageFn>(trampoline);
    g_define_image_hook_installed = true;
    return true;
}

bool InstallKnownSafeTextGlyphGetHeightHook(std::string* error) {
    std::scoped_lock lock(g_text_glyph_height_hook_mutex);
    if (g_text_glyph_get_height_hook_installed) {
        return true;
    }

    const HMODULE module = GetModuleHandleA(kOiramlookModuleName);
    if (!module) {
        if (error) {
            *error = "OIRAMLOOK.dll is not loaded";
        }
        return false;
    }

    auto* target = reinterpret_cast<std::uint8_t*>(
        GetProcAddress(module, kTextGlyphGetHeightSymbol));
    if (!target) {
        if (error) {
            *error = "GetProcAddress failed for RichText::TextGlyph::getHeight";
        }
        return false;
    }

    if (std::memcmp(
            target,
            kTextGlyphGetHeightExpectedBytes.data(),
            kTextGlyphGetHeightExpectedBytes.size()) != 0) {
        if (error) {
            *error = "unexpected RichText::TextGlyph::getHeight prologue";
        }
        return false;
    }

    if (!WriteRelativeJump(
            target,
            reinterpret_cast<void*>(&HookedTextGlyphGetHeight),
            kTextGlyphGetHeightPatchSpan,
            error)) {
        return false;
    }

    g_text_glyph_get_height_hook_installed = true;
    return true;
}

bool InstallKnownSafeTextGlyphCacheGlyphHook(std::string* error) {
    std::scoped_lock lock(g_text_glyph_height_hook_mutex);
    if (g_text_glyph_cache_glyph_hook_installed) {
        return true;
    }

    const HMODULE module = GetModuleHandleA(kOiramlookModuleName);
    if (!module) {
        if (error) {
            *error = "OIRAMLOOK.dll is not loaded";
        }
        return false;
    }

    auto* target = reinterpret_cast<std::uint8_t*>(
        GetProcAddress(module, kTextGlyphCacheGlyphSymbol));
    if (!target) {
        if (error) {
            *error = "GetProcAddress failed for RichText::TextGlyph::cacheGlyph";
        }
        return false;
    }

    if (std::memcmp(
            target,
            kTextGlyphCacheGlyphExpectedPrefix.data(),
            kTextGlyphCacheGlyphExpectedPrefix.size()) != 0) {
        if (error) {
            *error = "unexpected RichText::TextGlyph::cacheGlyph prologue";
        }
        return false;
    }

    void* trampoline = VirtualAlloc(
        nullptr,
        kTextGlyphCacheGlyphPatchSpan + 5,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!trampoline) {
        if (error) {
            *error = "VirtualAlloc failed for RichText::TextGlyph::cacheGlyph trampoline";
        }
        return false;
    }

    std::memcpy(trampoline, target, kTextGlyphCacheGlyphPatchSpan);
    void* continue_at = target + kTextGlyphCacheGlyphPatchSpan;
    if (!WriteRelativeJump(
            static_cast<std::uint8_t*>(trampoline) + kTextGlyphCacheGlyphPatchSpan,
            continue_at,
            5,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    if (!WriteRelativeJump(
            target,
            reinterpret_cast<void*>(&HookedTextGlyphCacheGlyph),
            kTextGlyphCacheGlyphPatchSpan,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    g_original_text_glyph_cache_glyph = reinterpret_cast<TextGlyphCacheGlyphFn>(trampoline);
    g_text_glyph_cache_glyph_hook_installed = true;
    return true;
}

std::string TryReadFontName(void* font) {
    if (!font) {
        return {};
    }

    CeguiBindings bindings{};
    std::string error;
    if (!TryGetCeguiBindings(&bindings, &error) ||
        !bindings.cegui_string_c_str) {
        return {};
    }

    const char* name = bindings.cegui_string_c_str(font);
    if (!name) {
        return {};
    }
    return std::string(name);
}

void MaybeLogGetCharAtPixelCall(
    void* font,
    const unsigned int start_char,
    const float pixel,
    const float original_scale,
    const float effective_scale,
    const unsigned int result) {
    constexpr unsigned int kMaxLoggedCalls = 16;
    const unsigned int index = g_logged_get_char_at_pixel_calls.fetch_add(1);
    if (index >= kMaxLoggedCalls) {
        return;
    }

    const std::string font_name = TryReadFontName(font);
    std::ostringstream detail;
    detail
        << "hook=get_char_at_pixel"
        << " font=" << font_name
        << " start=" << start_char
        << " pixel=" << pixel
        << " scale_in=" << original_scale
        << " scale_effective=" << effective_scale
        << " result=" << result;
    AppendHookEventLog(HookId::load_font_file, detail.str());
}

std::optional<DynamicFontOversamplePlan> GetOversamplePlanForFont(void* font) {
    std::scoped_lock lock(g_mutex);
    const auto it = g_oversampled_fonts.find(font);
    if (it == g_oversampled_fonts.end()) {
        return std::nullopt;
    }
    return it->second;
}

void RememberOversampledFont(void* font, const DynamicFontOversamplePlan& plan) {
    std::scoped_lock lock(g_mutex);
    g_oversampled_fonts[font] = plan;
}

void ForgetOversampledFont(void* font) {
    std::scoped_lock lock(g_mutex);
    g_oversampled_fonts.erase(font);
}

class ScopedCompensatedDrawText {
public:
    explicit ScopedCompensatedDrawText(void* font) noexcept
        : previous_font_(g_compensated_draw_text_font) {
        g_compensated_draw_text_font = font;
        ++g_compensated_draw_text_depth;
    }

    ScopedCompensatedDrawText(const ScopedCompensatedDrawText&) = delete;
    ScopedCompensatedDrawText& operator=(const ScopedCompensatedDrawText&) = delete;

    ~ScopedCompensatedDrawText() noexcept {
        if (g_compensated_draw_text_depth > 0) {
            --g_compensated_draw_text_depth;
        }
        g_compensated_draw_text_font = previous_font_;
    }

private:
    void* previous_font_ = nullptr;
};

bool IsInsideCompensatedDrawTextForFont(void* font) noexcept {
    return g_compensated_draw_text_depth > 0 && g_compensated_draw_text_font == font;
}

float GetEffectiveHorizontalMetricScale(
    const DynamicFontOversamplePlan& plan,
    void* font,
    const float x_scale) noexcept {
    if (IsInsideCompensatedDrawTextForFont(font)) {
        return x_scale;
    }
    return x_scale * plan.extent_scale;
}

void ScaleFontVerticalMetrics(void* font, const DynamicFontOversamplePlan& plan) {
    if (!font) {
        return;
    }

    auto* metrics = static_cast<float*>(font);
    metrics[kFontHeightIndex] *= plan.draw_scale;
    metrics[kLineSpacingIndex] *= plan.draw_scale;
    metrics[kBaselineIndex] *= plan.draw_scale;
    metrics[kLineSpacingIndex] *= plan.line_spacing_scale;
    metrics[kBaselineIndex] *= plan.baseline_scale;
    if (metrics[kLineSpacingIndex] > metrics[kFontHeightIndex]) {
        metrics[kLineSpacingIndex] = metrics[kFontHeightIndex];
    }
}

FontVerticalMetrics CaptureFontVerticalMetrics(void* font) noexcept {
    FontVerticalMetrics metrics{};
    if (!font) {
        return metrics;
    }

    const auto* values = static_cast<const float*>(font);
    metrics.font_height = values[kFontHeightIndex];
    metrics.line_spacing = values[kLineSpacingIndex];
    metrics.baseline = values[kBaselineIndex];
    return metrics;
}

void RestoreFontVerticalMetrics(void* font, const FontVerticalMetrics& metrics) noexcept {
    if (!font) {
        return;
    }

    auto* values = static_cast<float*>(font);
    values[kFontHeightIndex] = metrics.font_height;
    values[kLineSpacingIndex] = metrics.line_spacing;
    values[kBaselineIndex] = metrics.baseline;
}

void ApplyPlannedVerticalMetricAdjustments(
    void* font,
    const FontVerticalMetrics& original_metrics,
    const DynamicFontOversamplePlan& plan) noexcept {
    if (!font) {
        return;
    }

    auto adjusted_metrics = original_metrics;
    adjusted_metrics.line_spacing *= plan.line_spacing_scale;
    adjusted_metrics.baseline *= plan.baseline_scale;
    if (adjusted_metrics.line_spacing <= 0.0F) {
        adjusted_metrics.line_spacing = original_metrics.line_spacing;
    }
    if (adjusted_metrics.baseline <= 0.0F) {
        adjusted_metrics.baseline = original_metrics.baseline;
    }
    if (adjusted_metrics.line_spacing > adjusted_metrics.font_height) {
        adjusted_metrics.line_spacing = adjusted_metrics.font_height;
    }
    RestoreFontVerticalMetrics(font, adjusted_metrics);
}

void* TryReadTextGlyphFont(void* glyph) noexcept {
    if (!glyph) {
        return nullptr;
    }
    return *reinterpret_cast<void**>(
        static_cast<std::uint8_t*>(glyph) + kTextGlyphFontOffset);
}

std::uint32_t TryReadFontPointSize(void* font) {
    CeguiBindings bindings{};
    std::string error;
    if (!TryGetCeguiBindings(&bindings, &error) || !bindings.font_get_point_size) {
        return 0;
    }
    return bindings.font_get_point_size(font);
}

std::uint32_t ResolveNominalPointSize(
    const std::string_view font_name,
    const std::uint32_t reported_point_size) noexcept {
    if (font_name == "system") {
        return 13;
    }
    if (font_name == "systemBold") {
        return 13;
    }
    if (font_name == "dialog_simsun") {
        return 20;
    }
    return reported_point_size;
}

DynamicFontOversamplePlan GetFallbackDialogSimsunPlan(
    void* font,
    const std::uint32_t point_size) {
    DynamicFontOversamplePlan plan{};
    if (TryReadFontName(font) != "dialog_simsun") {
        return plan;
    }

    // If OIRAMLOOK sees a Font pointer before the runtime map is populated,
    // keep the same compensation constants as the oversample experiment.
    plan.apply = true;
    plan.oversampled_point_size = point_size;
    plan.draw_scale = 0.5F;
    plan.extent_scale = 0.5F;
    plan.line_spacing_scale = 1.0F;
    plan.baseline_scale = 1.0F;
    return plan;
}

void MaybeLogTextGlyphHeightCall(
    void* font,
    const std::uint32_t point_size,
    const float raw_height,
    const float effective_height,
    const bool compensated) {
    const unsigned int index = g_logged_text_glyph_height_calls.fetch_add(1);
    if (index >= kMaxLoggedTextGlyphHeightCalls) {
        return;
    }

    std::ostringstream detail;
    detail
        << "hook=text_glyph_get_height"
        << " font=" << TryReadFontName(font)
        << " point_size=" << point_size
        << " raw=" << raw_height
        << " effective=" << effective_height
        << " compensated=" << (compensated ? 1 : 0);
    AppendHookEventLog(HookId::load_font_file, detail.str());
}

std::string TryReadCeguiStringValue(const void* text) {
    if (!text) {
        return {};
    }

    CeguiBindings bindings{};
    std::string error;
    if (!TryGetCeguiBindings(&bindings, &error) || !bindings.cegui_string_c_str) {
        return {};
    }

    const char* raw = bindings.cegui_string_c_str(text);
    if (!raw) {
        return {};
    }
    return raw;
}

void MaybeLogTextGlyphCacheGlyphCall(
    void* font,
    const void* text,
    const float* rect,
    const float raw_height,
    const float effective_height) {
    const unsigned int index = g_logged_text_glyph_cache_calls.fetch_add(1);
    if (index >= kMaxLoggedTextGlyphCacheCalls) {
        return;
    }
    if (!rect) {
        return;
    }

    std::string text_value = TryReadCeguiStringValue(text);
    if (text_value.size() > 24) {
        text_value.resize(24);
        text_value += "...";
    }

    const float top_before = rect[1] - raw_height - 1.0F;
    const float top_after = rect[1] - effective_height - 1.0F;
    std::ostringstream detail;
    detail
        << "hook=text_glyph_cache_glyph"
        << " font=" << TryReadFontName(font)
        << " text=\"" << text_value << "\""
        << " line_rect=" << rect[0] << "," << rect[1] << "," << rect[2] << "," << rect[3]
        << " raw=" << raw_height
        << " effective=" << effective_height
        << " glyph_top_before=" << top_before
        << " glyph_top_after=" << top_after;
    AppendHookEventLog(HookId::load_font_file, detail.str());
}

float __fastcall HookedTextGlyphGetHeight(void* glyph, void*) {
    void* font = TryReadTextGlyphFont(glyph);
    if (!font) {
        return 0.0F;
    }

    const float raw_height = *(static_cast<float*>(font) + kLineSpacingIndex);
    if (raw_height <= 24.0F) {
        return raw_height;
    }

    DynamicFontOversamplePlan plan{};
    if (const auto known_plan = GetOversamplePlanForFont(font); known_plan.has_value()) {
        plan = *known_plan;
    } else {
        if (TryReadFontName(font) != "dialog_simsun") {
            return raw_height;
        }
        plan.apply = true;
        plan.draw_scale = 0.5F;
        plan.extent_scale = 0.5F;
        plan.line_spacing_scale = 1.0F;
        plan.baseline_scale = 1.0F;
    }

    const std::uint32_t point_size = TryReadFontPointSize(font);
    if (plan.oversampled_point_size == 0) {
        plan = GetFallbackDialogSimsunPlan(font, point_size);
    }

    const float effective_height =
        ComputeDialogRichTextGlyphHeight(plan, point_size, raw_height);
    MaybeLogTextGlyphHeightCall(font, point_size, raw_height, effective_height,
        effective_height != raw_height);
    return effective_height;
}

void __fastcall HookedTextGlyphCacheGlyph(
    void* glyph,
    void*,
    const void* rect,
    float z,
    void* render_cache,
    const void* text,
    const void* clip_rect) {
    if (!g_original_text_glyph_cache_glyph) {
        return;
    }

    void* font = TryReadTextGlyphFont(glyph);
    if (!font) {
        g_original_text_glyph_cache_glyph(glyph, rect, z, render_cache, text, clip_rect);
        return;
    }

    const float raw_height = *(static_cast<float*>(font) + kLineSpacingIndex);
    DynamicFontOversamplePlan plan{};
    if (const auto known_plan = GetOversamplePlanForFont(font); known_plan.has_value()) {
        plan = *known_plan;
    } else {
        const std::uint32_t point_size = TryReadFontPointSize(font);
        plan = GetFallbackDialogSimsunPlan(font, point_size);
    }

    if (!plan.apply || raw_height <= 24.0F) {
        g_original_text_glyph_cache_glyph(glyph, rect, z, render_cache, text, clip_rect);
        return;
    }

    const std::uint32_t point_size = TryReadFontPointSize(font);
    const float effective_height =
        ComputeDialogRichTextGlyphHeight(plan, point_size, raw_height);
    MaybeLogTextGlyphCacheGlyphCall(font, text, static_cast<const float*>(rect), raw_height, effective_height);

    auto* metrics = static_cast<float*>(font);
    const float previous_line_spacing = metrics[kLineSpacingIndex];
    metrics[kLineSpacingIndex] = effective_height;
    g_original_text_glyph_cache_glyph(glyph, rect, z, render_cache, text, clip_rect);
    metrics[kLineSpacingIndex] = previous_line_spacing;
}

int __fastcall HookedFontDrawText(
    void* font,
    void*,
    void* text,
    float* rect,
    float z,
    void* clipper,
    int formatting,
    void* colour_rect,
    float x_scale,
    float y_scale) {
    if (!g_original_font_draw_text) {
        return 0;
    }

    if (const auto plan = GetOversamplePlanForFont(font); plan.has_value()) {
        x_scale *= plan->draw_scale;
        y_scale *= plan->draw_scale;
        const ScopedCompensatedDrawText compensated_draw_text(font);
        return g_original_font_draw_text(
            font,
            text,
            rect,
            z,
            clipper,
            formatting,
            colour_rect,
            x_scale,
            y_scale);
    }
    return g_original_font_draw_text(
        font,
        text,
        rect,
        z,
        clipper,
        formatting,
        colour_rect,
        x_scale,
        y_scale);
}

double __fastcall HookedFontGetTextExtent(
    void* font,
    void*,
    const void* text,
    float x_scale) {
    if (!g_original_font_get_text_extent) {
        return 0.0;
    }

    if (const auto plan = GetOversamplePlanForFont(font); plan.has_value()) {
        return g_original_font_get_text_extent(
            font,
            text,
            GetEffectiveHorizontalMetricScale(*plan, font, x_scale));
    }
    return g_original_font_get_text_extent(font, text, x_scale);
}

unsigned int __fastcall HookedFontGetCharAtPixel(
    void* font,
    void*,
    const void* text,
    unsigned int start_char,
    float pixel,
    float x_scale) {
    if (!g_original_font_get_char_at_pixel) {
        return start_char;
    }

    float effective_scale = x_scale;
    if (const auto plan = GetOversamplePlanForFont(font); plan.has_value()) {
        effective_scale = GetEffectiveHorizontalMetricScale(*plan, font, x_scale);
    }
    const unsigned int result = g_original_font_get_char_at_pixel(
        font,
        text,
        start_char,
        pixel,
        effective_scale);
    MaybeLogGetCharAtPixelCall(font, start_char, pixel, x_scale, effective_scale, result);
    return result;
}

double __fastcall HookedFontGetFormattedTextExtent(
    void* font,
    void*,
    void* text,
    void* rect,
    int formatting,
    float x_scale) {
    if (!g_original_font_get_formatted_text_extent) {
        return 0.0;
    }

    if (const auto plan = GetOversamplePlanForFont(font); plan.has_value()) {
        return g_original_font_get_formatted_text_extent(
            font,
            text,
            rect,
            formatting,
            GetEffectiveHorizontalMetricScale(*plan, font, x_scale));
    }
    return g_original_font_get_formatted_text_extent(font, text, rect, formatting, x_scale);
}

float __fastcall HookedFontGetWrappedTextExtent(
    void* font,
    void*,
    const void* text,
    float wrap_width,
    float x_scale) {
    if (!g_original_font_get_wrapped_text_extent) {
        return 0.0F;
    }

    if (const auto plan = GetOversamplePlanForFont(font); plan.has_value()) {
        return g_original_font_get_wrapped_text_extent(
            font,
            text,
            wrap_width,
            GetEffectiveHorizontalMetricScale(*plan, font, x_scale));
    }
    return g_original_font_get_wrapped_text_extent(font, text, wrap_width, x_scale);
}

void __fastcall HookedImagesetDefineImage(
    void* imageset,
    void*,
    const void* name,
    const void* source_rect,
    const void* offset) {
    if (!g_original_imageset_define_image) {
        return;
    }

    const float offset_y = g_active_define_image_offset_y;
    if (offset && offset_y != 0.0F) {
        CeguiVector2Value adjusted_offset =
            *static_cast<const CeguiVector2Value*>(offset);
        adjusted_offset.y += offset_y;

        const unsigned int index = g_logged_define_image_adjustments.fetch_add(1);
        if (index < 16) {
            CeguiBindings bindings{};
            std::string binding_error;
            std::string glyph_name;
            if (TryGetCeguiBindings(&bindings, &binding_error) &&
                bindings.cegui_string_c_str) {
                if (const char* raw_name = bindings.cegui_string_c_str(name)) {
                    glyph_name = raw_name;
                }
            }

            std::ostringstream detail;
            detail
                << "hook=imageset_define_image"
                << " font=" << g_active_define_image_font_name
                << " glyph=" << glyph_name
                << " offset_y_before=" << static_cast<const CeguiVector2Value*>(offset)->y
                << " offset_y_after=" << adjusted_offset.y;
            AppendHookEventLog(HookId::load_font_file, detail.str());
        }

        g_original_imageset_define_image(
            imageset,
            name,
            source_rect,
            &adjusted_offset);
        return;
    }

    g_original_imageset_define_image(imageset, name, source_rect, offset);
}

}  // namespace

bool ApplyDynamicFontOversampleExperiment(
    void* font,
    const bool enable_rich_text_compensation,
    std::string* error) {
    if (!font) {
        if (error) {
            *error = "font pointer is null";
        }
        return false;
    }

    CeguiBindings bindings{};
    if (!TryGetCeguiBindings(&bindings, error)) {
        return false;
    }
    if (!bindings.font_get_point_size || !bindings.font_create_font_from_ft_face) {
        if (error) {
            *error = "dynamic font experiment bindings are unavailable";
        }
        return false;
    }

    const std::string font_name = TryReadFontName(font);
    const std::uint32_t point_size =
        ResolveNominalPointSize(font_name, bindings.font_get_point_size(font));
    const auto plan = BuildDynamicFontOversamplePlan(font_name, point_size);
    if (!plan.apply) {
        if (error) {
            *error = "dynamic font oversample plan does not apply";
        }
        return false;
    }

    if (!InstallKnownSafeDrawTextHook(error) ||
        !InstallKnownSafeGetTextExtentHook(error) ||
        !InstallKnownSafeGetCharAtPixelHook(error) ||
        !InstallKnownSafeGetFormattedTextExtentHook(error) ||
        !InstallKnownSafeGetWrappedTextExtentHook(error)) {
        return false;
    }
    bool define_image_hook_installed = false;
    if (plan.glyph_offset_y != 0.0F) {
        define_image_hook_installed = InstallKnownSafeDefineImageHook(error);
        if (!define_image_hook_installed) {
            return false;
        }
    }
    bool rich_text_height_hook_installed = false;
    std::string rich_text_height_detail;
    if (enable_rich_text_compensation) {
        rich_text_height_hook_installed =
            EnsureDialogRichTextGlyphHeightCompensationHook(&rich_text_height_detail);
    }

    const FontVerticalMetrics original_vertical_metrics =
        CaptureFontVerticalMetrics(font);
    const GlyphImageOffsetScope glyph_offset_scope(font_name, plan.glyph_offset_y);
    bindings.font_create_font_from_ft_face(font, plan.oversampled_point_size, 0, 0);
    if (plan.preserve_original_vertical_metrics) {
        ApplyPlannedVerticalMetricAdjustments(font, original_vertical_metrics, plan);
    } else {
        ScaleFontVerticalMetrics(font, plan);
    }
    RememberOversampledFont(font, plan);

    if (error) {
        *error = "oversampled_point_size=" + std::to_string(plan.oversampled_point_size) +
            " draw_scale=" + std::to_string(plan.draw_scale) +
            " extent_scale=" + std::to_string(plan.extent_scale) +
            " glyph_offset_y=" + std::to_string(plan.glyph_offset_y) +
            " line_spacing_scale=" + std::to_string(plan.line_spacing_scale) +
            " baseline_scale=" + std::to_string(plan.baseline_scale) +
            " preserve_original_vertical_metrics=" +
            (plan.preserve_original_vertical_metrics
                ? std::string("1")
                : std::string("0")) +
            " vertical_metrics_scaled=" +
            (plan.preserve_original_vertical_metrics
                ? std::string("0")
                : std::string("1")) +
            " char_at_pixel_scaled=1" +
            " define_image_hook=" +
            (define_image_hook_installed ? std::string("1") : std::string("0")) +
            " metric_double_scale_guard=1" +
            " rich_text_height_hook=" +
            (rich_text_height_hook_installed ? std::string("1") : std::string("0")) +
            " rich_text_cache_glyph_hook=" +
            (rich_text_height_hook_installed ? std::string("1") : std::string("0")) +
            (rich_text_height_detail.empty()
                ? std::string()
                : std::string(" rich_text_height_detail=\"") + rich_text_height_detail + "\"");
    }
    return true;
}

bool RestoreDynamicFontOversampleExperiment(void* font, std::string* error) {
    if (!font) {
        if (error) {
            *error = "font pointer is null";
        }
        return false;
    }

    CeguiBindings bindings{};
    if (!TryGetCeguiBindings(&bindings, error)) {
        return false;
    }
    if (!bindings.font_create_font_from_ft_face) {
        if (error) {
            *error = "dynamic font restore binding is unavailable";
        }
        return false;
    }

    const std::string font_name = TryReadFontName(font);
    const std::uint32_t point_size =
        ResolveNominalPointSize(font_name, bindings.font_get_point_size(font));
    if (point_size == 0) {
        if (error) {
            *error = "font point size is unavailable";
        }
        return false;
    }

    bindings.font_create_font_from_ft_face(font, point_size, 0, 0);
    ForgetOversampledFont(font);
    if (error) {
        *error =
            "restored_point_size=" + std::to_string(point_size) +
            " oversample=0";
    }
    return true;
}

bool EnsureDialogRichTextGlyphHeightCompensationHook(std::string* error) {
    std::string height_error;
    if (!InstallKnownSafeTextGlyphGetHeightHook(&height_error)) {
        if (error) {
            *error = height_error;
        }
        return false;
    }

    std::string cache_error;
    if (!InstallKnownSafeTextGlyphCacheGlyphHook(&cache_error)) {
        if (error) {
            *error = cache_error;
        }
        return false;
    }

    if (error) {
        *error = "text_glyph_get_height=1 text_glyph_cache_glyph=1";
    }
    return true;
}

}  // namespace pal4::inject

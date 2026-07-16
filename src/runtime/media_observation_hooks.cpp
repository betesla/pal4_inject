#include "media_observation_hooks.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>

#include <windows.h>

#include "hook_logging.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using AudioSystemPlayMusicFn = std::uint32_t* (__thiscall*)(
    void*, std::uint32_t*, const char*, int);
using GiPlayMovieFn = int (__cdecl*)(void*);
using BinkPlayerOpenVideoFn = char (__thiscall*)(void*, const char*, float, char);

AudioSystemPlayMusicFn g_original_audio_system_play_music = nullptr;
GiPlayMovieFn g_original_gi_play_movie = nullptr;
BinkPlayerOpenVideoFn g_original_bink_player_open_video = nullptr;

std::string ReadBoundedCString(const char* const address) {
    if (!address) {
        return {};
    }
    constexpr std::size_t kMaxLength = 511;
    std::string value;
    value.reserve(128);
    for (std::size_t offset = 0; offset < kMaxLength; ++offset) {
        char byte = '\0';
        SIZE_T bytes_read = 0;
        if (!ReadProcessMemory(
                GetCurrentProcess(),
                address + offset,
                &byte,
                sizeof(byte),
                &bytes_read) ||
            bytes_read != sizeof(byte)) {
            return {};
        }
        if (byte == '\0') {
            return value;
        }
        value.push_back(byte);
    }
    return value;
}

std::string LowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string ResourceStem(const std::string& resource) {
    const auto slash = resource.find_last_of("/\\");
    const auto begin = slash == std::string::npos ? 0 : slash + 1;
    const auto dot = resource.find_last_of('.');
    const auto end = dot == std::string::npos || dot < begin ? resource.size() : dot;
    return resource.substr(begin, end - begin);
}

std::string ReadGameStringArg(void* const argument) {
    if (!argument) {
        return {};
    }
    const char* text = nullptr;
    SIZE_T bytes_read = 0;
    if (!ReadProcessMemory(
            GetCurrentProcess(),
            static_cast<const unsigned char*>(argument) + sizeof(std::uint32_t),
            &text,
            sizeof(text),
            &bytes_read) ||
        bytes_read != sizeof(text)) {
        return {};
    }
    return ReadBoundedCString(text);
}

std::uint32_t* __fastcall Hook_AudioSystemPlayMusic(
    void* const self,
    void*,
    std::uint32_t* const result_out,
    const char* const resource_arg,
    const int streaming) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::audio_system_play_music);

    const auto resource = ReadBoundedCString(resource_arg);
    std::uint32_t* result = result_out;
    if (g_original_audio_system_play_music) {
        result = g_original_audio_system_play_music(
            self, result_out, resource_arg, streaming);
    } else {
        state.SetHookError(
            HookId::audio_system_play_music,
            "original AudioSystem::PlayMusic trampoline is null");
        return result;
    }

    const auto lowered = LowerAscii(resource);
    if (lowered.find("palsound") != std::string::npos &&
        lowered.ends_with(".mp3")) {
        const bool success = result_out && *result_out != 0;
        std::ostringstream out;
        out
            << "hook=audio_system_play_music media=voice event=open"
            << " voice_key=" << ResourceStem(resource)
            << " success=" << (success ? 1 : 0)
            << " resource=" << resource;
        AppendCriticalHookEventLog(out.str());
    }
    state.ClearHookError(HookId::audio_system_play_music);
    return result;
}

int __cdecl Hook_GiPlayMovie(void* const resource_arg) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::gi_play_movie);
    const auto resource = ReadGameStringArg(resource_arg);
    std::ostringstream out;
    out
        << "hook=gi_play_movie media=bink event=request"
        << " resource=" << (resource.empty() ? "unknown" : resource);
    AppendCriticalHookEventLog(out.str());

    if (!g_original_gi_play_movie) {
        state.SetHookError(
            HookId::gi_play_movie,
            "original giPlayMovie trampoline is null");
        return 0;
    }
    const int result = g_original_gi_play_movie(resource_arg);
    state.ClearHookError(HookId::gi_play_movie);
    return result;
}

char __fastcall Hook_BinkPlayerOpenVideo(
    void* const self,
    void*,
    const char* const resource_arg,
    const float volume,
    const char fit_to_screen) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::bink_player_open_video);
    const auto resource = ReadBoundedCString(resource_arg);

    if (!g_original_bink_player_open_video) {
        state.SetHookError(
            HookId::bink_player_open_video,
            "original BinkPlayer::OpenVideo trampoline is null");
        return 0;
    }

    const char result = g_original_bink_player_open_video(
        self, resource_arg, volume, fit_to_screen);
    std::ostringstream out;
    out
        << "hook=bink_player_open_video media=bink event=open"
        << " success=" << (result ? 1 : 0)
        << " resource=" << (resource.empty() ? "unknown" : resource);
    AppendCriticalHookEventLog(out.str());
    state.ClearHookError(HookId::bink_player_open_video);
    return result;
}

}  // namespace

void* GetMediaObservationReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::audio_system_play_music:
        return reinterpret_cast<void*>(&Hook_AudioSystemPlayMusic);
    case HookId::gi_play_movie:
        return reinterpret_cast<void*>(&Hook_GiPlayMovie);
    case HookId::bink_player_open_video:
        return reinterpret_cast<void*>(&Hook_BinkPlayerOpenVideo);
    default:
        return nullptr;
    }
}

void SetMediaObservationOriginalTrampoline(const HookId id, void* const trampoline) {
    switch (id) {
    case HookId::audio_system_play_music:
        g_original_audio_system_play_music =
            reinterpret_cast<AudioSystemPlayMusicFn>(trampoline);
        break;
    case HookId::gi_play_movie:
        g_original_gi_play_movie = reinterpret_cast<GiPlayMovieFn>(trampoline);
        break;
    case HookId::bink_player_open_video:
        g_original_bink_player_open_video =
            reinterpret_cast<BinkPlayerOpenVideoFn>(trampoline);
        break;
    default:
        break;
    }
}

}  // namespace pal4::inject

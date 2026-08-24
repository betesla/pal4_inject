#include "media_observation_hooks.h"

#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>

#include <windows.h>

#include "hook_logging.h"
#include "pal4inject/dialogue_voice_volume.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using AudioSystemPlaySampleFn = std::uint32_t* (__thiscall*)(
    void*, std::uint32_t*, const char*, int);
using AudioSampleSetVolumeFn = int (__thiscall*)(void*);
using GiPlayMovieFn = int (__cdecl*)(void*);
using BinkPlayerOpenVideoFn = char (__thiscall*)(void*, const char*, float, char);

AudioSystemPlaySampleFn g_original_audio_system_play_sample = nullptr;
GiPlayMovieFn g_original_gi_play_movie = nullptr;
BinkPlayerOpenVideoFn g_original_bink_player_open_video = nullptr;
thread_local unsigned int g_gi_talk_voice_request_depth = 0;
std::atomic<void*> g_last_gi_talk_audio_manager{nullptr};
std::atomic<std::uint32_t> g_last_gi_talk_sample_id{0};

template <typename T>
bool ReadCurrentProcessValue(const void* const address, T* const out) {
    if (!address || !out) {
        return false;
    }
    SIZE_T bytes_read = 0;
    return ReadProcessMemory(
               GetCurrentProcess(), address, out, sizeof(T), &bytes_read) != FALSE &&
        bytes_read == sizeof(T);
}

bool IsExecutableAddress(const void* const address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    const DWORD protection = info.Protect & 0xFFU;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

bool ApplyGiTalkVolumeToSample(
    void* const audio_manager,
    const std::uint32_t sample_id,
    const float multiplier,
    std::string* const error) {
    // AudioSystem+0x30 is the vector object; its begin/end fields are +4/+8.
    constexpr std::size_t kActiveSamplesBeginOffset = 52;
    constexpr std::size_t kActiveSamplesEndOffset = 56;
    constexpr std::size_t kSampleIdOffset = 4;
    constexpr std::size_t kSampleVolumeMultiplierOffset = 20;
    constexpr std::size_t kSetVolumeVtableIndex = 11;
    constexpr std::size_t kMaxActiveSamples = 4096;

    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    const auto* const manager_bytes = static_cast<const unsigned char*>(audio_manager);
    if (!ReadCurrentProcessValue(manager_bytes + kActiveSamplesBeginOffset, &begin) ||
        !ReadCurrentProcessValue(manager_bytes + kActiveSamplesEndOffset, &end) ||
        begin == 0 || end < begin || (end - begin) % sizeof(void*) != 0 ||
        (end - begin) / sizeof(void*) > kMaxActiveSamples) {
        if (error) {
            *error = "invalid active AudioSample vector";
        }
        return false;
    }

    for (std::uintptr_t cursor = begin; cursor < end; cursor += sizeof(void*)) {
        void* sample = nullptr;
        if (!ReadCurrentProcessValue(reinterpret_cast<const void*>(cursor), &sample) || !sample) {
            continue;
        }
        std::uint32_t object_id = 0;
        const auto* const sample_bytes = static_cast<const unsigned char*>(sample);
        if (!ReadCurrentProcessValue(sample_bytes + kSampleIdOffset, &object_id) ||
            object_id != sample_id) {
            continue;
        }

        void** vtable = nullptr;
        if (!ReadCurrentProcessValue(sample, &vtable) || !vtable) {
            if (error) {
                *error = "AudioSample vtable is unreadable";
            }
            return false;
        }
        void* setter_address = nullptr;
        if (!ReadCurrentProcessValue(vtable + kSetVolumeVtableIndex, &setter_address) ||
            !IsExecutableAddress(setter_address)) {
            if (error) {
                *error = "AudioSample::SetVolume slot is invalid";
            }
            return false;
        }

        SIZE_T bytes_written = 0;
        if (!WriteProcessMemory(
                GetCurrentProcess(),
                const_cast<unsigned char*>(sample_bytes) + kSampleVolumeMultiplierOffset,
                &multiplier,
                sizeof(multiplier),
                &bytes_written) ||
            bytes_written != sizeof(multiplier)) {
            if (error) {
                *error = "failed to write AudioSample volume multiplier";
            }
            return false;
        }

        reinterpret_cast<AudioSampleSetVolumeFn>(setter_address)(sample);
        if (error) {
            error->clear();
        }
        return true;
    }

    if (error) {
        *error = "created AudioSample id was not found";
    }
    return false;
}

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
    if (g_original_audio_system_play_sample) {
        result = g_original_audio_system_play_sample(
            self, result_out, resource_arg, streaming);
    } else {
        state.SetHookError(
            HookId::audio_system_play_music,
            "original AudioSystem::PlayMusic trampoline is null");
        return result;
    }

    if (IsGiTalkVoiceResource(resource)) {
        const bool success = result_out && *result_out != 0;
        const bool gi_talk_request = IsGiTalkVoiceRequestActive();
        bool volume_applied = false;
        std::string volume_error;
        const float multiplier = state.GetGiTalkVolume();
        if (gi_talk_request) {
            if (success) {
                g_last_gi_talk_audio_manager.store(self, std::memory_order_release);
                g_last_gi_talk_sample_id.store(*result_out, std::memory_order_release);
                volume_applied = ApplyGiTalkVolumeToSample(
                    self, *result_out, multiplier, &volume_error);
            } else {
                g_last_gi_talk_sample_id.store(0, std::memory_order_release);
                volume_error = "AudioSample creation failed";
            }
            std::ostringstream summary;
            summary << "voice_key=" << ResourceStem(resource)
                    << " multiplier=" << multiplier;
            if (!volume_applied) {
                summary << " error=" << volume_error;
            }
            state.SetGiTalkVolumeApplied(volume_applied, summary.str());
        }
        std::ostringstream out;
        out
            << "hook=audio_system_play_music media=voice event=open"
            << " voice_key=" << ResourceStem(resource)
            << " success=" << (success ? 1 : 0)
            << " gi_talk_request=" << (gi_talk_request ? 1 : 0)
            << " volume_multiplier=" << multiplier
            << " volume_applied=" << (volume_applied ? 1 : 0);
        if (!volume_error.empty()) {
            out << " volume_error=" << volume_error;
        }
        out
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

ScopedGiTalkVoiceRequest::ScopedGiTalkVoiceRequest() noexcept {
    ++g_gi_talk_voice_request_depth;
}

ScopedGiTalkVoiceRequest::~ScopedGiTalkVoiceRequest() {
    if (g_gi_talk_voice_request_depth != 0) {
        --g_gi_talk_voice_request_depth;
    }
}

bool IsGiTalkVoiceRequestActive() noexcept {
    return g_gi_talk_voice_request_depth != 0;
}

bool RefreshActiveGiTalkVoiceVolume(std::string* const error) {
    const auto sample_id = g_last_gi_talk_sample_id.load(std::memory_order_acquire);
    void* const audio_manager =
        g_last_gi_talk_audio_manager.load(std::memory_order_acquire);
    auto& state = GetRuntimeState();
    const float multiplier = state.GetGiTalkVolume();
    if (!audio_manager || sample_id == 0) {
        state.SetGiTalkVolumeApplied(false, "waiting for next giTalk voice");
        if (error) {
            *error = "no active giTalk voice";
        }
        return false;
    }

    std::string apply_error;
    const bool applied = ApplyGiTalkVolumeToSample(
        audio_manager, sample_id, multiplier, &apply_error);
    std::ostringstream summary;
    if (applied) {
        summary << "active_voice_id=" << sample_id
                << " multiplier=" << multiplier;
    } else {
        g_last_gi_talk_sample_id.store(0, std::memory_order_release);
        summary << "waiting for next giTalk voice";
    }
    state.SetGiTalkVolumeApplied(applied, summary.str());
    if (error) {
        *error = apply_error;
    }
    return applied;
}

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
        g_original_audio_system_play_sample =
            reinterpret_cast<AudioSystemPlaySampleFn>(trampoline);
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

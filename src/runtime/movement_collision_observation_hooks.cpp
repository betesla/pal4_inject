#include "movement_collision_observation_hooks.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "runtime_state.h"

namespace pal4::inject {
namespace {

using MovementCollisionCheckFn = int (__thiscall*)(
    void*, float*, float*, int, float*, std::uint32_t*, int, int);

MovementCollisionCheckFn g_original_movement_collision_check = nullptr;
std::atomic<std::uint32_t> g_logged_calls{0};

template <std::size_t N>
bool ReadFloats(const float* source, std::array<float, N>* destination) {
    if (!source || !destination) {
        return false;
    }
    SIZE_T bytes_read = 0;
    return ReadProcessMemory(
               GetCurrentProcess(), source, destination->data(),
               sizeof(float) * N, &bytes_read) != FALSE &&
           bytes_read == sizeof(float) * N;
}

int __fastcall Hook_MovementCollisionCheck(
    void* self,
    void*,
    float* from,
    float* to,
    int ignored_atomic,
    float* movement,
    std::uint32_t* result_data,
    int arg7,
    int arg8) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::movement_collision_check);

    std::array<float, 4> from_values{};
    std::array<float, 4> to_values{};
    std::array<float, 3> movement_values{};
    const bool from_ok = ReadFloats(from, &from_values);
    const bool to_ok = ReadFloats(to, &to_values);
    const bool movement_ok = ReadFloats(movement, &movement_values);

    const int result = g_original_movement_collision_check
        ? g_original_movement_collision_check(
              self, from, to, ignored_atomic, movement, result_data, arg7, arg8)
        : 0;

    const std::uint32_t log_index = g_logged_calls.fetch_add(1);
    if (log_index < 256 || result != 0) {
        std::ostringstream log;
        log << "hook=movement_collision_check event=checked"
            << " sequence=" << (log_index + 1)
            << " result=" << result
            << " ignored_atomic=0x" << std::hex
            << static_cast<std::uint32_t>(ignored_atomic) << std::dec
            << " arg7=" << arg7
            << " arg8=" << arg8;
        if (from_ok) {
            log << " from=" << from_values[0] << ',' << from_values[1] << ','
                << from_values[2] << ',' << from_values[3];
        }
        if (to_ok) {
            log << " to=" << to_values[0] << ',' << to_values[1] << ','
                << to_values[2] << ',' << to_values[3];
        }
        if (movement_ok) {
            log << " movement=" << movement_values[0] << ',' << movement_values[1]
                << ',' << movement_values[2];
        }
        state.AppendEventLog(log.str());
    }

    if (!g_original_movement_collision_check) {
        state.SetHookError(
            HookId::movement_collision_check,
            "original movement collision trampoline is null");
    }
    return result;
}

}  // namespace

void* GetMovementCollisionReplacementForHook(const HookId id) {
    return id == HookId::movement_collision_check
        ? reinterpret_cast<void*>(&Hook_MovementCollisionCheck)
        : nullptr;
}

void SetMovementCollisionOriginalTrampoline(const HookId id, void* trampoline) {
    if (id == HookId::movement_collision_check) {
        g_original_movement_collision_check =
            reinterpret_cast<MovementCollisionCheckFn>(trampoline);
    }
}

}  // namespace pal4::inject

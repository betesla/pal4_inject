#include "combat_observation_hooks.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <intrin.h>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "hook_logging.h"
#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using CombatHandleActionFn = int (__thiscall*)(void*, const std::int32_t*);
using CombatCreateStuntActionFn = void* (__cdecl*)(void*, void*, void*);
using CombatExecuteStuntFn = char (__thiscall*)(void*);
using CombatSkillDamageFn = float* (__cdecl*)(float*, void*, void*, float, float);
using CombatSystemEndFn = unsigned int (__cdecl*)(void*);
using GameDbGetInstanceFn = void* (__cdecl*)();
using GameDbFindStuntByIdFn = void* (__thiscall*)(void*, std::int32_t);

CombatHandleActionFn g_original_combat_handle_action = nullptr;
CombatCreateStuntActionFn g_original_combat_create_stunt_action = nullptr;
CombatExecuteStuntFn g_original_combat_execute_stunt = nullptr;
CombatSkillDamageFn g_original_combat_skill_damage = nullptr;
CombatSystemEndFn g_original_combat_system_end = nullptr;

std::atomic<std::uint64_t> g_action_sequence{0};
std::atomic<std::uint64_t> g_stunt_sequence{0};
std::atomic<std::uint64_t> g_skill_damage_sequence{0};
std::atomic<std::int32_t> g_last_stunt_id{0};

std::uintptr_t MainModuleBase() {
    auto& state = GetRuntimeState();
    std::uintptr_t base = state.MainModuleBase();
    if (base == 0) {
        base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
        state.SetMainModuleBase(base);
    }
    return base;
}

template <typename Fn>
Fn ResolveRuntimeFunction(const std::uint32_t ida_ea) {
    const auto base = MainModuleBase();
    return base == 0
        ? nullptr
        : reinterpret_cast<Fn>(ida::ResolveRuntimeAddress(base, ida_ea));
}

std::int32_t ResolveTrackedStuntId(void* stunt_record) {
    if (!stunt_record) {
        return 0;
    }
    const auto get_game_db = ResolveRuntimeFunction<GameDbGetInstanceFn>(
        ida::kGameDbGetInstanceInternal);
    const auto find_stunt = ResolveRuntimeFunction<GameDbFindStuntByIdFn>(
        ida::kGameDbFindStuntById);
    if (!get_game_db || !find_stunt) {
        return 0;
    }
    auto* game_db = static_cast<std::uint8_t*>(get_game_db());
    if (!game_db) {
        return 0;
    }
    void* stunt_table = game_db + 12;
    for (const std::int32_t stunt_id : {5843, 5844}) {
        if (find_stunt(stunt_table, stunt_id) == stunt_record) {
            return stunt_id;
        }
    }
    return 0;
}

const char* ClassifyStuntCaller(const void* caller) {
    const auto base = MainModuleBase();
    const auto address = reinterpret_cast<std::uintptr_t>(caller);
    if (base == 0 || address < base) {
        return "unknown";
    }
    const auto ida_ea = static_cast<std::uint32_t>(
        ida::kLaunchExeBase + (address - base));
    return ida_ea >= ida::kAiSelectStuntBegin && ida_ea < ida::kAiSelectStuntEnd
        ? "ai"
        : "player";
}

template <typename T>
bool ReadLocalMemory(const T* source, T* destination) {
    if (!source || !destination) {
        return false;
    }
    SIZE_T bytes_read = 0;
    return ReadProcessMemory(
               GetCurrentProcess(),
               source,
               destination,
               sizeof(T),
               &bytes_read) != FALSE &&
           bytes_read == sizeof(T);
}

int __fastcall Hook_CombatHandleAction(
    void* self,
    void*,
    const std::int32_t* action) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::combat_handle_action);

    const auto sequence = g_action_sequence.fetch_add(1) + 1;
    std::array<std::int32_t, 4> fields{};
    const bool readable = ReadLocalMemory(
        reinterpret_cast<const std::array<std::int32_t, 4>*>(action),
        &fields);
    if (readable && fields[3] == 1) {
        g_last_stunt_id.store(fields[2]);
        g_stunt_sequence.fetch_add(1);
    }

    std::ostringstream log;
    log << "hook=combat_handle_action event=dispatch"
        << " sequence=" << sequence
        << " readable=" << (readable ? 1 : 0);
    if (readable) {
        log << " action_type=" << fields[3]
            << " asset_id=" << fields[2]
            << " target_token=" << fields[1];
    }
    AppendCriticalHookEventLog(log.str());

    if (!g_original_combat_handle_action) {
        state.SetHookError(
            HookId::combat_handle_action,
            "original combat_handle_action trampoline is null");
        state.SetLastError("original combat_handle_action trampoline is null");
        return 0;
    }
    state.ClearHookError(HookId::combat_handle_action);
    return g_original_combat_handle_action(self, action);
}

void* __cdecl Hook_CombatCreateStuntAction(
    void* stunt_record,
    void* actor,
    void* target) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::combat_create_stunt_action);

    const void* caller = _ReturnAddress();
    const std::int32_t stunt_id = ResolveTrackedStuntId(stunt_record);
    const char* source = ClassifyStuntCaller(caller);
    if (stunt_id != 0) {
        g_last_stunt_id.store(stunt_id);
        g_stunt_sequence.fetch_add(1);
    }

    if (!g_original_combat_create_stunt_action) {
        state.SetHookError(
            HookId::combat_create_stunt_action,
            "original combat stunt-action trampoline is null");
        state.SetLastError("original combat stunt-action trampoline is null");
        return nullptr;
    }

    void* result = g_original_combat_create_stunt_action(
        stunt_record,
        actor,
        target);
    state.ClearHookError(HookId::combat_create_stunt_action);

    std::ostringstream log;
    log << "hook=combat_create_stunt_action event=created"
        << " source=" << source
        << " stunt_id=" << stunt_id
        << " task_created=" << (result ? 1 : 0)
        << " stunt_record=" << stunt_record
        << " actor=" << actor
        << " target=" << target;
    AppendCriticalHookEventLog(log.str());
    return result;
}

char __fastcall Hook_CombatExecuteStunt(void* self, void*) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::combat_execute_stunt);

    // script_stuntTask_handleActor receives the embedded task at outer+0x100;
    // its +0x44 member is the same Stunt GameDB record passed to
    // scriptCombatAction3. Resolve by pointer equality so the observer does
    // not depend on an undocumented record layout or a stale global id.
    void* stunt_record = nullptr;
    const bool record_readable = ReadLocalMemory(
        reinterpret_cast<void* const*>(
            static_cast<std::uint8_t*>(self) + 0x44),
        &stunt_record);
    const std::int32_t stunt_id = record_readable
        ? ResolveTrackedStuntId(stunt_record)
        : 0;

    if (!g_original_combat_execute_stunt) {
        state.SetHookError(
            HookId::combat_execute_stunt,
            "original combat_execute_stunt trampoline is null");
        state.SetLastError("original combat_execute_stunt trampoline is null");
        return 0;
    }

    const char result = g_original_combat_execute_stunt(self);
    state.ClearHookError(HookId::combat_execute_stunt);

    std::ostringstream log;
    log << "hook=combat_execute_stunt event=executed"
        << " stunt_id=" << stunt_id
        << " record_readable=" << (record_readable ? 1 : 0)
        << " result=" << static_cast<int>(result)
        << " stunt_record=" << stunt_record
        << " task=" << self;
    AppendCriticalHookEventLog(log.str());
    return result;
}

float* __cdecl Hook_CombatSkillDamage(
    float* output,
    void* target,
    void* attacker,
    const float element,
    const float multiplier) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::combat_skill_damage);

    if (!g_original_combat_skill_damage) {
        state.SetHookError(
            HookId::combat_skill_damage,
            "original combat_skill_damage trampoline is null");
        state.SetLastError("original combat_skill_damage trampoline is null");
        return output;
    }

    float* result = g_original_combat_skill_damage(
        output,
        target,
        attacker,
        element,
        multiplier);
    state.ClearHookError(HookId::combat_skill_damage);

    const auto sequence = g_skill_damage_sequence.fetch_add(1) + 1;
    std::array<float, 2> deltas{};
    const bool readable = ReadLocalMemory(
        reinterpret_cast<const std::array<float, 2>*>(result),
        &deltas);
    std::ostringstream log;
    log << "hook=combat_skill_damage event=calculated"
        << " sequence=" << sequence
        << " source_asset_id=" << g_last_stunt_id.load()
        << " readable=" << (readable ? 1 : 0)
        << " element=" << element
        << " multiplier=" << multiplier;
    if (readable) {
        log << " hp_delta=" << deltas[0]
            << " secondary_delta=" << deltas[1];
    }
    AppendCriticalHookEventLog(log.str());
    return result;
}

unsigned int __cdecl Hook_CombatSystemEnd(void* combat_system) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::combat_system_end);

    std::ostringstream begin_log;
    begin_log << "hook=combat_system_end event=begin"
              << " actions=" << g_action_sequence.load()
              << " stunts=" << g_stunt_sequence.load()
              << " skill_damage_calls=" << g_skill_damage_sequence.load()
              << " last_stunt_id=" << g_last_stunt_id.load();
    AppendCriticalHookEventLog(begin_log.str());

    if (!g_original_combat_system_end) {
        state.SetHookError(
            HookId::combat_system_end,
            "original combat_system_end trampoline is null");
        state.SetLastError("original combat_system_end trampoline is null");
        return 0;
    }

    const unsigned int result = g_original_combat_system_end(combat_system);
    state.ClearHookError(HookId::combat_system_end);

    std::ostringstream complete_log;
    complete_log << "hook=combat_system_end event=complete result=" << result;
    AppendCriticalHookEventLog(complete_log.str());
    return result;
}

}  // namespace

void* GetCombatObservationReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::combat_handle_action:
        return reinterpret_cast<void*>(&Hook_CombatHandleAction);
    case HookId::combat_create_stunt_action:
        return reinterpret_cast<void*>(&Hook_CombatCreateStuntAction);
    case HookId::combat_execute_stunt:
        return reinterpret_cast<void*>(&Hook_CombatExecuteStunt);
    case HookId::combat_skill_damage:
        return reinterpret_cast<void*>(&Hook_CombatSkillDamage);
    case HookId::combat_system_end:
        return reinterpret_cast<void*>(&Hook_CombatSystemEnd);
    default:
        return nullptr;
    }
}

void SetCombatObservationOriginalTrampoline(
    const HookId id,
    void* const trampoline) {
    switch (id) {
    case HookId::combat_handle_action:
        g_original_combat_handle_action =
            reinterpret_cast<CombatHandleActionFn>(trampoline);
        break;
    case HookId::combat_create_stunt_action:
        g_original_combat_create_stunt_action =
            reinterpret_cast<CombatCreateStuntActionFn>(trampoline);
        break;
    case HookId::combat_execute_stunt:
        g_original_combat_execute_stunt =
            reinterpret_cast<CombatExecuteStuntFn>(trampoline);
        break;
    case HookId::combat_skill_damage:
        g_original_combat_skill_damage =
            reinterpret_cast<CombatSkillDamageFn>(trampoline);
        break;
    case HookId::combat_system_end:
        g_original_combat_system_end =
            reinterpret_cast<CombatSystemEndFn>(trampoline);
        break;
    default:
        break;
    }
}

}  // namespace pal4::inject

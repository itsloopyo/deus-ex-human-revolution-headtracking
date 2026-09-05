#include "ads.h"

#include "logging.h"

#include "MinHook.h"

#include <windows.h>

#include <atomic>
#include <cstdint>

namespace DeusExHumanRevolutionHeadTracking {

namespace {

// The iron-sight controller's per-frame update is thiscall taking the frame's
// delta time, which the prologue reads from [EBP+8]. The detour has to carry
// that argument or it returns without cleaning the caller's push, leaking four
// bytes of stack per frame. __fastcall puts this in ECX, the unused EDX slot
// next and the float on the stack, and cleans up callee-side exactly as the
// original does.
using IronSightUpdateFn = void(__fastcall*)(void* self, void* edx, float deltaTime);

// The player's own state test: thiscall, one stack argument, returning the
// answer in AL. It walks the five state layers hanging off the player and says
// whether any of them holds the id it was given, which is how the game's own
// iron-sight code asks the question.
using IsInStateFn = char(__fastcall*)(void* self, void* edx, int stateId);

IronSightUpdateFn s_original = nullptr;
IsInStateFn       s_isInState = nullptr;
uint32_t          s_ownerOffset = 0;
int               s_stateId = 0;

std::atomic<bool>               s_aiming{false};
std::atomic<unsigned long long> s_stampMs{0};
bool s_loggedFirst = false;

// How old the last answer may be before it stops counting. The controller ticks
// with the frame, so anything past a few frames means the player controller has
// stopped updating - a menu, a load, a cinematic - and the honest answer there
// is that the sights are not up.
constexpr unsigned long long kStaleMs = 200;

void __fastcall IronSightUpdateDetour(void* self, void* edx, float deltaTime) {
    s_original(self, edx, deltaTime);

    // The player the controller hangs off. The engine dereferences it two
    // instructions into this function without checking, so neither do we.
    void* player = *reinterpret_cast<void**>(
        static_cast<uint8_t*>(self) + s_ownerOffset);
    const bool aiming = s_isInState(player, nullptr, s_stateId) != 0;

    s_aiming.store(aiming, std::memory_order_relaxed);
    s_stampMs.store(GetTickCount64(), std::memory_order_release);

    if (aiming && !s_loggedFirst) {
        s_loggedFirst = true;
        Log::Line("AdsHook: the game reports the sights up (player state 0x%02X)", s_stateId);
    }
}

}  // namespace

bool SightsAreUp() {
    const unsigned long long stamp = s_stampMs.load(std::memory_order_acquire);
    if (GetTickCount64() - stamp > kStaleMs) {
        return false;
    }
    return s_aiming.load(std::memory_order_relaxed);
}

bool AdsHook::Install(const BuildProfile& profile) {
    if (m_installed) return true;

    HMODULE base = GetModuleHandleA(profile.module);
    if (!base) {
        Log::Line("ERROR: AdsHook could not resolve the game module");
        return false;
    }
    uintptr_t moduleBase = reinterpret_cast<uintptr_t>(base);

    s_isInState = reinterpret_cast<IsInStateFn>(moduleBase + profile.playerStateQueryRva);
    s_ownerOffset = profile.ironSightOwnerOffset;
    s_stateId = profile.ironSightStateId;

    void* target = reinterpret_cast<void*>(moduleBase + profile.ironSightUpdateRva);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&IronSightUpdateDetour),
                      reinterpret_cast<void**>(&s_original)) != MH_OK) {
        Log::Line("ERROR: MH_CreateHook(iron-sight update @ %p) failed", target);
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log::Line("ERROR: MH_EnableHook(iron-sight update) failed");
        return false;
    }

    Log::Line("AdsHook installed: target=%p state=0x%02X", target, s_stateId);
    m_installed = true;
    return true;
}

void AdsHook::Uninstall() {
    m_installed = false;
}

}  // namespace DeusExHumanRevolutionHeadTracking

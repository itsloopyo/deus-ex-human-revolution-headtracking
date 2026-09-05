#include "lean_trace.h"

#include "logging.h"

#include <windows.h>

#include <cmath>
#include <cstdint>

namespace DeusExHumanRevolutionHeadTracking {
namespace lean_trace {

namespace {

using cameraunlock::camera::LeanObstruction;
using cameraunlock::math::Vec3;

// The collision world's line trace, taken off three independent call sites in
// DXHRDC.exe: the weapon's aim cast (FUN_00804850), the engine's generic trace
// wrapper (FUN_00762640) and the game's own camera collision (FUN_0069d890).
// All three call the same vtable slot on the same singleton with the same
// argument shape:
//
//   MOV ECX,[collisionWorld]  ; this
//   PUSH count                ; how many entries the ignore list holds
//   PUSH &ignore              ; entities the trace passes through
//   PUSH &hit                 ; 64-byte result, hit position at +0x10
//   PUSH &end                 ; vec4, 16-byte aligned
//   PUSH &start               ; vec4, 16-byte aligned
//   PUSH kind                 ; which collision set to trace against
//   CALL [[ECX]+0x54]         ; -> AL, non-zero on a hit
//
// Nothing pops the arguments at any of the three sites, so the callee cleans:
// __fastcall with a dummy EDX puts `this` in ECX and leaves the rest on the
// stack, which is the same idiom the camera hook's own detour uses.
using TraceFn = char(__fastcall*)(void* self, void* edx, std::uint32_t kind,
                                  const float* start, const float* end, void* outHit,
                                  void* const* ignore, int ignoreCount);

// The result buffer's size, not a guess: all three call sites reserve exactly
// 0x40 bytes for it and the whole of that is inside their frames.
constexpr int kHitSize = 0x40;
// Where the trace writes the world point it stopped at. Both the weapon cast
// and the camera collision read their hit position from here.
constexpr int kHitPositionOffset = 0x10;

std::uintptr_t s_moduleBase = 0;
std::uintptr_t s_moduleEnd = 0;
std::uint32_t  s_collisionWorldRva = 0;
std::uint32_t  s_gameContextRva = 0;
std::uint32_t  s_nearPlaneRva = 0;
std::uint32_t  s_traceVfunc = 0;
std::uint32_t  s_queryKind = 0;
bool  s_installed = false;
float s_eyeW = 1.0f;
bool  s_loggedUnavailable = false;
bool  s_loggedReady = false;

// Whether a pointer the ENGINE is about to dereference is worth handing it. The
// collision world and the player entity are read out of globals that are null
// before a level is up and stale for a moment after one is torn down, and a
// trace called with either of them wrong is a crash inside engine code rather
// than a bad answer. This is the boundary; past it the contract is trusted.
bool Readable(const void* p, std::size_t n) {
    if (p == nullptr) return false;
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if ((mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
    const std::uintptr_t regionEnd =
        reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return reinterpret_cast<std::uintptr_t>(p) + n <= regionEnd;
}

// Why the last PlayerEntity() call came back empty, so a failure names the link
// of the chain that broke instead of the whole chain.
const char* s_playerFault = "";

void Unavailable(const char* why) {
    if (s_loggedUnavailable) return;
    s_loggedUnavailable = true;
    Log::Line("lean-trace: %s. The lean runs UNCLAMPED until this clears - head "
              "position still works, but leaning into a wall puts the view through "
              "it.", why);
}

// The player, from the same global the game's own camera collision reads it
// from. FUN_0069d890 does, in order:
//
//   MOV EDX,[gameContext]   ; the context object
//   MOV EAX,[EDX]           ; its first member
//   MOV EDI,[EAX+4]         ; the entity, which then goes in the ignore list
//
// The eye sits inside the player's own collision, so without this every trace
// reports something solid at zero distance and the lean stops working
// everywhere.
void* PlayerEntity() {
    const void* holder = reinterpret_cast<const void*>(s_moduleBase + s_gameContextRva);
    if (!Readable(holder, sizeof(void*))) {
        s_playerFault = "the game context global is not mapped";
        return nullptr;
    }
    const std::uintptr_t context = *reinterpret_cast<const std::uintptr_t*>(holder);
    if (!Readable(reinterpret_cast<const void*>(context), sizeof(void*))) {
        s_playerFault = "there is no game context yet";
        return nullptr;
    }
    const std::uintptr_t owner = *reinterpret_cast<const std::uintptr_t*>(context);
    if (!Readable(reinterpret_cast<const void*>(owner), 8)) {
        s_playerFault = "the game context holds no owner yet";
        return nullptr;
    }
    void* entity = *reinterpret_cast<void* const*>(owner + 4);
    if (!Readable(entity, sizeof(void*))) {
        s_playerFault = "the owner names no player entity yet";
        return nullptr;
    }
    return entity;
}

TraceFn ResolveTrace(void*& outWorld) {
    const void* slot = reinterpret_cast<const void*>(s_moduleBase + s_collisionWorldRva);
    if (!Readable(slot, sizeof(void*))) return nullptr;
    void* world = *reinterpret_cast<void* const*>(slot);
    if (!Readable(world, sizeof(void*))) return nullptr;

    void** vtable = *reinterpret_cast<void***>(world);
    if (!Readable(vtable, s_traceVfunc + sizeof(void*))) return nullptr;
    const std::uintptr_t fn = reinterpret_cast<std::uintptr_t>(
        *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(vtable) + s_traceVfunc));
    // The trace lives in the EXE. A vtable pointer that survives the read above
    // but whose slot points outside the image is not this object's vtable.
    if (fn < s_moduleBase || fn >= s_moduleEnd) return nullptr;

    outWorld = world;
    return reinterpret_cast<TraceFn>(fn);
}

}  // namespace

void Install(const BuildProfile& profile) {
    HMODULE module = GetModuleHandleA(profile.module);  // null -> the EXE
    if (module == nullptr) return;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const std::uint8_t*>(module) + dos->e_lfanew);

    s_moduleBase = reinterpret_cast<std::uintptr_t>(module);
    s_moduleEnd = s_moduleBase + nt->OptionalHeader.SizeOfImage;
    s_collisionWorldRva = profile.collisionWorldRva;
    s_gameContextRva = profile.gameContextRva;
    s_nearPlaneRva = profile.renderNearPlaneRva;
    s_traceVfunc = profile.collisionTraceVfunc;
    s_queryKind = profile.collisionQueryKind;
    s_installed = true;
}

void SetEyeW(float w) { s_eyeW = w; }

float NearPlaneUnits() {
    if (!s_installed) return 0.0f;
    const void* p = reinterpret_cast<const void*>(s_moduleBase + s_nearPlaneRva);
    if (!Readable(p, sizeof(float))) return 0.0f;
    const float value = *reinterpret_cast<const float*>(p);
    // The engine floors its own near plane at 40 units and pairs it with a
    // 12000-unit far plane, so anything outside this window is a frame the
    // renderer has not written yet rather than a near plane worth honouring.
    if (!std::isfinite(value) || value < 1.0f || value > 1000.0f) return 0.0f;
    return value;
}

LeanObstruction Query(void*, const Vec3& start, const Vec3& direction, float maxDistance) {
    LeanObstruction out;
    if (!s_installed) return out;

    void* world = nullptr;
    TraceFn trace = ResolveTrace(world);
    if (trace == nullptr) {
        Unavailable("the collision world is not readable");
        return out;
    }
    void* player = PlayerEntity();
    if (player == nullptr) {
        // Without the player in the ignore list the trace starts inside the
        // player's own collision, so it is a hit at zero distance every frame -
        // an answer that would silently pin every lean to nothing. Reporting no
        // answer at all is the honest result.
        Unavailable(s_playerFault);
        return out;
    }

    // The engine loads both endpoints with MOVAPS, the aligned SSE read, so an
    // unaligned buffer faults inside engine code with a junk address rather than
    // returning a wrong answer.
    alignas(16) float from[4] = {start.x, start.y, start.z, s_eyeW};
    alignas(16) float to[4] = {start.x + direction.x * maxDistance,
                               start.y + direction.y * maxDistance,
                               start.z + direction.z * maxDistance,
                               s_eyeW};
    alignas(16) unsigned char hit[kHitSize] = {};

    // Two slots because every call site reserves two, and the count says how
    // many the trace reads. The player is the one thing the eye is always
    // inside; nothing else needs excluding from a sweep this short.
    void* ignore[2] = {player, nullptr};

    const char blocked = trace(world, nullptr, s_queryKind, from, to, hit, ignore, 1);

    out.queried = true;
    if (blocked != 0) {
        const float* point = reinterpret_cast<const float*>(hit + kHitPositionOffset);
        const float dx = point[0] - start.x;
        const float dy = point[1] - start.y;
        const float dz = point[2] - start.z;
        out.blocked = true;
        out.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    if (!s_loggedReady) {
        s_loggedReady = true;
        Log::Line("lean-trace: the game's collision world answered (world=%p player=%p "
                  "near-plane=%.0f units, wanted %.1f units, %s)",
                  world, player, NearPlaneUnits(), maxDistance,
                  out.blocked ? "blocked" : "clear");
    }
    return out;
}

}  // namespace lean_trace
}  // namespace DeusExHumanRevolutionHeadTracking

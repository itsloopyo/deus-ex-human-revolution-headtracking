#include "build_profile.h"

#include "logging.h"

#include <windows.h>

namespace DeusExHumanRevolutionHeadTracking {

using cameraunlock::memory::PeFingerprint;
using cameraunlock::memory::FingerprintMismatch;

// ---- profile registry (append-only; newest first) -------------------------
//
// Steam Director's Cut build, DXHRDC.exe, fingerprinted 2026-06-03.
// CameraManager::Update bakes a 4x4 camera matrix into CameraManager+0x13B0
// each frame (CameraManager::GetMatrix returns exactly that address). RVA
// derived from an MSVC RTTI vtable walk of the shipped EXE.
//
// 316.05 is the engine's own world-units-per-metre constant, read from
// .rdata:0x00AA447C - the gameplay code multiplies by it wherever it prints or
// compares a distance in metres.
// Camera reads whose result has to agree with the image on screen rather than
// with where the player is aiming. Two build the rendered view: FUN_00618c50
// copies the transform into the renderer's camera cache, flips the view-space y
// row and hands it to the renderer singleton, and FUN_0057f610 stages the
// render context alongside it. The other two are the engine's world-to-screen
// projections, which place HUD markers and tooltips - project those through the
// clean camera and they land where the player is aiming instead of where they
// are looking. The fifth is the pick ray behind every world-space panel - the
// elevator keypad, and anything else that puts a cursor on a surface: it
// unprojects the cursor through the camera and intersects the panel's own
// transform, so served the clean camera it tests a different key from the one
// drawn under the cursor. A cursor is not a weapon; it has to follow the drawn
// view, which is why decoupling it is wrong here and right for the aim cast.
// All five are return addresses, so each is the instruction after its call.
static const uint32_t kSteamRenderReads_20260603[] = {
    0x00218CA6,  // FUN_00618c50, render camera setup
    0x0017F89F,  // FUN_0057f610, render context staging
    0x0040C4B8,  // FUN_0080c490, world-to-screen projection
    0x0041C3BE,  // FUN_0081c3a0, world-to-screen projection
    0x00301E2F,  // FUN_00701d20, cursor pick ray for world-space panels
};

// The sights being up is player state 0x68, which is how the game's own
// iron-sight code asks: FUN_007b9590 (the `ironsightleave` binding) tests it
// before it does anything, FUN_00699040 (can-enter) tests its negation, and
// FUN_00699480 - the iron-sight controller's per-frame update, ticked by
// NsPlayerController::Update - gates its whole body on it. That last one is
// what this mod hooks, asking the predicate with the controller's own player
// pointer, its first member.
// The field of view, and the units trap in it.
//
// `CameraManager::Update` (FUN_006a15c0) ends with
// `this[0x4fd] = tan(activeCamera->vfunc[0x50]() * 0.5)`, which is the
// tan(fov/2) at +0x13F4 the reticle projects through. vfunc[0x50] is
// `GenericCamera::GetFov` (FUN_006a9050): the camera's own FOV at +0x30, in
// radians, clamped to a 3.0416 rad ceiling and a 0.1 rad floor.
//
// That FOV is VERTICAL. The engine authors fields of view horizontally and
// derives the vertical from the aspect - FUN_006a48a0 is literally
// `this->fovV = 2 * atan(tan(fovH / 2) / aspect)`, defaulting the aspect to
// 16:9 when it reads as invalid - and the engine's own world-to-screen
// (FUN_0081c4f0) passes tan(GetFov()/2) and the aspect from FUN_00617e00
// (= DAT_00e0523c) to FUN_0081c3a0, which divides x by the second.
//
// So the three values logged in play - 0.5625 walking around, 0.4316 with the
// sights up, 0.2330 scoped - are tan(vertical/2) on a 16:9 frame, and
// multiplying by 16/9 gives 1.00000, 0.76733 and 0.41421: horizontal fields of
// view of exactly 90, 75 and 45 degrees. Three round numbers is what pins the
// un-zoomed one at 90 horizontal.
//
// GenericCamera's constructor sets +0x30 from DAT_00aa81e4 = pi/2, which is a
// vertical placeholder gameplay overwrites, NOT the base. Treating it as the
// base scaled all of normal play by 0.5625.
static const BuildProfile kSteamProfile_20260603 = {
    "steam-win32-20260603",
    nullptr,  // null module = the main EXE (GetModuleHandle(NULL))
    {
        0x52840914,  // TimeDateStamp
        0x01C54000,  // SizeOfImage
        0x00B633A7,  // CheckSum
    },
    0x002A15C0,  // CameraManager::Update RVA
    0x000013B0,  // baked 4x4 matrix offset within CameraManager
    0x00000030,  // active-camera pointer within CameraManager
    0x00000048,  // camera vtable: GetMatrix -> its own 4x4
    0x0000003C,  // camera vtable: GetPosition -> vec4
    0x00000020,  // camera pitch/roll/yaw (radians)
    0x00000040,  // camera-to-world transform (rows = basis, row 3 = position)
    0x00000044,  // camera vtable: GetWorldTransform -> +0x40
    kSteamRenderReads_20260603,
    5,
    0x000013F4,  // CameraManager: tan(fov/2), recomputed each frame
    0x00000050,  // camera vtable: GetFov -> vertical radians (FUN_006a9050)
    0x00A0523C,  // the engine's aspect, DAT_00e0523c (VA 0x00E0523C)
    90.0f,       // the un-zoomed horizontal field of view
    0x00405230,  // NsReticleMovieController::Update (vtable slot 6)
    0x00000020,  // the controller's GFx movie
    0x00404850,  // FUN_00804850 clean-aim cast -> metres
    0x00000060,  // GFx movie vtable: SetVariable(path, &value)
    0x00328360,  // FUN_00728360 movie stage dimensions
    0x00000024,  // GFx movie vtable: visible rect
    0x0041C4F0,  // FUN_0081c4f0 world-to-screen, derives its own FOV and aspect
    0x00299480,  // FUN_00699480 iron-sight controller update, ticked by the player controller
    0x00000000,  // the player object, first member of that controller
    0x003804E0,  // FUN_007804e0 IsInState(player, id)
    0x68,        // the state id the game's own iron-sight code tests for
    0x0187BF64,  // the collision world singleton (VA 0x01C7BF64)
    0x00000054,  // its vtable: LineTrace(kind, start, end, hit, ignore, count)
    0xC72C1BA3,  // the query kind the weapon cast and the camera collision use
    0x01607B1C,  // the game context holder (VA 0x01A07B1C); player at [[it]]+4
    0x0184C5C8,  // the frame's near clip distance (VA 0x01C4C5C8), in world units
    316.05f,     // world units per metre
};

static const BuildProfile* kKnownProfiles[] = {
    &kSteamProfile_20260603,
};
static constexpr int kProfileCount =
    static_cast<int>(sizeof(kKnownProfiles) / sizeof(kKnownProfiles[0]));

// ---------------------------------------------------------------------------

static bool FingerprintModule(const char* moduleName, PeFingerprint& out) {
    HMODULE mod = GetModuleHandleA(moduleName);  // null -> the process EXE
    if (!mod) return false;
    return cameraunlock::memory::ReadPeFingerprint(mod, out);
}

const BuildProfile* MatchRunningBuild() {
    for (int i = 0; i < kProfileCount; i++) {
        const BuildProfile* p = kKnownProfiles[i];
        PeFingerprint running{};
        if (!FingerprintModule(p->module, running)) continue;
        if (running.Matches(p->fingerprint)) return p;
    }
    return nullptr;
}

void LogUnmatchedBuildDiagnostic() {
    static_assert(kProfileCount > 0, "build registry must hold at least one profile");
    const BuildProfile* primary = kKnownProfiles[0];

    PeFingerprint running{};
    if (!FingerprintModule(primary->module, running)) {
        Log::Line("WARN: Could not read the game EXE fingerprint - mod dormant.");
        return;
    }

    // All three fields, because authoring the profile for this build needs all
    // three and the mismatch lines below only quote TimeDateStamp.
    Log::Line("Running DXHRDC.exe fingerprint: TimeDateStamp=0x%08X SizeOfImage=0x%08X "
              "CheckSum=0x%08X",
              running.TimeDateStamp, running.SizeOfImage, running.CheckSum);

    switch (cameraunlock::memory::ClassifyMismatch(running, primary->fingerprint)) {
        case FingerprintMismatch::Newer:
            Log::Line(
                "WARN: This DXHRDC.exe is NEWER than any build this mod knows "
                "(TimeDateStamp 0x%08X > 0x%08X). Check the releases page for an "
                "updated mod. Head tracking is dormant.",
                running.TimeDateStamp, primary->fingerprint.TimeDateStamp);
            break;
        case FingerprintMismatch::Older:
            Log::Line(
                "WARN: This DXHRDC.exe is OLDER than this mod's newest known build "
                "(TimeDateStamp 0x%08X < 0x%08X). Let Steam finish updating. "
                "Head tracking is dormant.",
                running.TimeDateStamp, primary->fingerprint.TimeDateStamp);
            break;
        case FingerprintMismatch::Differs:
            Log::Line(
                "WARN: This DXHRDC.exe has the expected TimeDateStamp but a "
                "different size/checksum (0x%08X/0x%08X vs 0x%08X/0x%08X) - "
                "tampered or repacked binary. Head tracking is dormant.",
                running.SizeOfImage, running.CheckSum,
                primary->fingerprint.SizeOfImage, primary->fingerprint.CheckSum);
            break;
    }
}

}  // namespace DeusExHumanRevolutionHeadTracking

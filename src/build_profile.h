#pragma once

#include "cameraunlock/memory/pe_fingerprint.h"

#include <cstdint>

namespace DeusExHumanRevolutionHeadTracking {

// Append-only build-profile registry. DXHR Director's Cut is ASLR-enabled, so
// the camera hook attaches at module base + RVA. RVAs are pinned to a specific
// shipped build identified by the running EXE's PE fingerprint; no match leaves
// the hook dormant rather than detouring stale addresses.
//
// NEVER edit an existing profile's RVAs in place and NEVER delete old
// profiles - that strands users who have not taken a patch. Add a new
// profile to the TOP of kKnownProfiles when a patch breaks the current build.

struct BuildProfile {
    const char* name;     // "steam-win32-YYYYMMDD"
    const char* module;   // module to fingerprint (the game EXE)
    cameraunlock::memory::PeFingerprint fingerprint;
    uint32_t cameraUpdateRva;  // RVA of CameraManager::Update (per-frame bake)
    uint32_t cameraMatrixOffset;  // byte offset of the baked 4x4 within CameraManager
    uint32_t activeCameraOffset;  // byte offset of the active-camera pointer within CameraManager
    uint32_t cameraMatrixVfunc;   // vtable byte offset of the camera's GetMatrix
    uint32_t cameraPositionVfunc; // vtable byte offset of the camera's GetPosition
    uint32_t cameraAnglesOffset;  // pitch/roll/yaw radians within the camera object
    uint32_t cameraWorldOffset;   // camera-to-world transform within the camera object
    uint32_t cameraWorldVfunc;    // vtable byte offset of the world-transform accessor
    // Return addresses of the camera reads that build the rendered view. These
    // get the head-tracked transform; every other reader gets the clean one.
    const uint32_t* renderReadRvas;
    int             renderReadCount;
    uint32_t cameraFovOffset;     // tan(fov/2) cached in CameraManager each frame
    // What it takes to know how far the game is zoomed. GetFov answers in
    // VERTICAL radians, so its tangent is multiplied by the aspect to reach the
    // horizontal field of view the game's own settings are authored in, and
    // compared against the un-zoomed one. That ratio is the zoom, and it is what
    // head tracking is scaled by so a scope cannot multiply how far the view
    // moves with the head.
    uint32_t cameraFovVfunc;      // camera vtable: GetFov -> vertical radians
    uint32_t aspectRva;           // the aspect the engine's own projection divides x by
    float    baseHorizontalFovDeg;  // the game's un-zoomed horizontal field of view
    uint32_t reticleUpdateRva;    // NsReticleMovieController::Update
    uint32_t reticleMovieOffset;  // the GFx movie within the reticle controller
    uint32_t reticleAimRva;       // the controller's clean-aim cast, returns metres
    uint32_t movieSetVariableVfunc;  // GFx movie vtable: SetVariable(path, value)
    uint32_t movieDimensionsRva;  // reads the movie's own stage width and height
    uint32_t movieVisibleRectVfunc;  // GFx movie vtable: visible rect, for letterboxing
    uint32_t worldToScreenRva;    // engine world-to-screen, for checking our own
    uint32_t ironSightUpdateRva;  // the iron-sight controller's per-frame update
    uint32_t ironSightOwnerOffset;  // the player object that controller hangs off
    uint32_t playerStateQueryRva; // bool(player, stateId): is the player in it
    int      ironSightStateId;    // the player state id that means the sights are up
    // The collision world singleton, and the vtable slot on it that traces a
    // line between two world points. The query kind selects which collision set
    // the trace runs against; the game passes the same one for its weapon cast
    // and for its own camera collision.
    uint32_t collisionWorldRva;
    uint32_t collisionTraceVfunc;
    uint32_t collisionQueryKind;
    // The game context holder. The player entity - the one thing the eye is
    // always inside - is two dereferences and a +4 away from it.
    uint32_t gameContextRva;
    // The near clip distance the renderer built this frame's projection from,
    // in world units. Read rather than assumed: it is the floor the lean
    // standoff has to clear, and a camera can raise it above the engine's own
    // minimum.
    uint32_t renderNearPlaneRva;
    float    unitsPerMetre;  // world units in one metre, read out of the EXE
};

// Finds the profile whose module is loaded and whose fingerprint matches.
// Returns nullptr if no known build matches (mod stays dormant).
const BuildProfile* MatchRunningBuild();

// Logs, for an unmatched build, whether the running EXE is newer or older
// than the diagnostic-primary profile (top of the registry).
void LogUnmatchedBuildDiagnostic();

}  // namespace DeusExHumanRevolutionHeadTracking

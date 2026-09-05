#include "reticle_hook.h"

#include "ads.h"
#include "aim_marker.h"
#include "camera_hook.h"
#include "logging.h"

#include "MinHook.h"

#include <windows.h>

#include <cmath>
#include <cstdint>

namespace DeusExHumanRevolutionHeadTracking {

namespace {

// NsReticleMovieController::Update is thiscall taking a float delta time, which
// the prologue reads from [EBP+8]. The detour has to carry that argument or it
// returns without cleaning the caller's push, leaking four bytes of stack per
// frame. __fastcall puts this in ECX, the unused EDX slot next, and the float
// on the stack, and cleans up callee-side exactly as the original does.
using ReticleUpdateFn = void(__fastcall*)(void* self, void* edx, float deltaTime);

// The controller's own aim cast: thiscall, no stack arguments - its prologue is
// MOV EBX,ECX with no [EBP+8] read - returning the distance in metres in ST0.
// It casts along the camera's forward row, and because the aim path is served
// the clean transform, that is the distance to what the shot would hit rather
// than to what the player is looking at.
using AimDistanceFn = float(__fastcall*)(void* self, void* edx);

// GFx movie: SetVariable(path, &value). Two stack arguments, this in ECX.
using SetVariableFn = void(__thiscall*)(void* self, const char* path, const void* value);

// Reads the movie's own stage size into out[0..1]. Thiscall with one out
// parameter, callee-cleaned - the call site pushes the buffer and does not
// adjust ESP afterwards. This is where _root._x's units come from: guessing the
// stage is guessing the whole scale of the correction.
using MovieDimensionsFn = float*(__thiscall*)(void* movie, float* out);

// The movie's visible rect. Thiscall with one out parameter, callee-cleaned,
// same shape as the dimensions call. This is the term that accounts for the
// movie not covering the screen exactly: a 16:9 stage in a window of any other
// shape is letterboxed, and without this the horizontal and vertical scales are
// wrong by the ratio between the two aspects.
using MovieVisibleRectFn = float*(__thiscall*)(void* movie, float* out);

// The engine's own complete world-to-screen: FUN_0081c4f0(out, point), two
// stack arguments, caller-cleaned. It returns 0..1 screen coordinates with y
// down, and - the reason we call it rather than reimplement it - it derives the
// FOV and the aspect itself.
//
// That matters because the camera exposes more than one field of view. The one
// cached at CameraManager+0x13F4 comes from vfunc[0x50] and lives in the same
// structure as a matrix nothing renders from; this path uses vfunc[0xf0]. Using
// the first put the reticle short of the target by the ratio between them,
// which no amount of checking the arithmetic could reveal, because the check
// handed our own FOV to the engine's formula and so only ever proved the two
// agreed with each other.
//
// It reads the camera through the accessor at a call site on the tracked list,
// so it projects through the view actually being drawn.
using ProjectFn = float*(__cdecl*)(float* out, const float* point);

// GFx::Value as the engine builds it: an interface pointer, a type tag, then
// the payload. Type 3 is a number, which is what FUN_0062fe50 writes when the
// engine pushes a numeric argument into Flash.
struct GfxValue {
    void*    iface;
    uint32_t type;
    double   number;
};
constexpr uint32_t kGfxNumber = 3;

ReticleUpdateFn s_originalUpdate = nullptr;
ProjectFn       s_engineProject = nullptr;
// Set once the projection has been checked against the engine's.
bool            s_validated = false;
AimDistanceFn   s_aimDistance = nullptr;
MovieDimensionsFn s_movieDimensions = nullptr;
MovieVisibleRectFn s_movieVisibleRect = nullptr;
uint32_t        s_visibleRectVfunc = 0;
float           s_unitsPerMetre = 0.0f;
uint32_t        s_movieOffset = 0;
uint32_t        s_setVariableVfunc = 0;
float           s_stageWidth = 0.0f;
float           s_stageHeight = 0.0f;
float           s_unitsPerScreenX = 0.0f;   // stage units across the full width
float           s_unitsPerScreenY = 0.0f;   // stage units down the full height
TrackingRuntime* s_tracking = nullptr;
bool            s_diag = false;
bool            s_probe = false;
bool            s_loggedProbe = false;

int  s_diagWritten = 0;
bool s_loggedFirst = false;
constexpr int kMaxDiagLines = 40;
constexpr int kDiagInterval = 60;
uint64_t s_frames = 0;

// _root._x is in the movie's own stage units, so the screen fraction has to be
// scaled by the stage the movie was authored at. Read it from the movie rather
// than assuming a common one: a wrong stage scales the entire correction, and
// shows up in play as a reticle that moves the right way but never quite
// reaches what the shot is pointing at.
bool ResolveStage(void* movie) {
    if (s_unitsPerScreenX > 0.0f) return true;

    alignas(16) float dims[4] = {};
    s_movieDimensions(movie, dims);

    // How much of the movie the screen actually shows. The engine divides by
    // this before scaling by the stage (FUN_008042d0), which is what keeps its
    // own HUD placement correct when the movie is letterboxed.
    alignas(16) float visible[4] = {};
    const uint8_t* vtable = *reinterpret_cast<const uint8_t* const*>(movie);
    MovieVisibleRectFn getRect =
        *reinterpret_cast<MovieVisibleRectFn const*>(vtable + s_visibleRectVfunc);
    getRect(movie, visible);

    if (!(dims[0] > 0.0f) || !(dims[1] > 0.0f) ||
        !(visible[0] > 0.0f) || !(visible[1] > 0.0f) ||
        !std::isfinite(dims[0]) || !std::isfinite(dims[1]) ||
        !std::isfinite(visible[0]) || !std::isfinite(visible[1])) {
        Log::Line("ERROR: the reticle movie reports stage %gx%g visible %gx%g; cannot "
                  "place the reticle without both", dims[0], dims[1], visible[0], visible[1]);
        return false;
    }

    s_stageWidth = dims[0];
    s_stageHeight = dims[1];
    s_unitsPerScreenX = dims[0] / visible[0];
    s_unitsPerScreenY = dims[1] / visible[1];
    Log::Line("ReticleHook: stage %.1fx%.1f visible %.4fx%.4f -> %.1f x %.1f stage units "
              "across the screen", dims[0], dims[1], visible[0], visible[1],
              s_unitsPerScreenX, s_unitsPerScreenY);
    return true;
}

void SetMovieNumber(void* movie, const char* path, float value) {
    const uint8_t* vtable = *reinterpret_cast<const uint8_t* const*>(movie);
    SetVariableFn setVariable =
        *reinterpret_cast<SetVariableFn const*>(vtable + s_setVariableVfunc);

    GfxValue v{};
    v.iface = nullptr;
    v.type = kGfxNumber;
    v.number = static_cast<double>(value);
    setVariable(movie, path, &v);
}

// Where the clean aim ray lands in the head-tracked view, as a fraction of the
// screen away from its centre.
//
// Basis-to-basis on the vectors the camera hook actually wrote, never a formula
// in yaw/pitch/roll: one derivation used twice cannot disagree with itself,
// whereas a per-axis tangent formula agrees on single-axis poses and drifts on
// combined ones. The denominator mirrors the engine's own world-to-screen
// projection (FUN_0081c3a0) - same basis rows, same aspect divisor on x, same
// 2*depth*tan(fov/2) - so the result lands in the coordinates the game itself
// places HUD elements in, and inherits its FOV and handedness conventions
// rather than guessing at them.
//
// The reticle marks a POINT, not a direction. With the head centred the two
// project alike, which is why a direction was enough at first. A lean breaks
// it: the frame is drawn from an eye tens of centimetres from the one the shot
// leaves, so a fixed direction slides off the thing it marks, worse the closer
// the target - the player leans, the reticle drifts off what they were aiming
// at, and the bullet carries on exactly where it was going. So the vector
// projected here runs from the RENDER eye to the point the shot lands on, and
// the parallax falls out of it.
//
// The distance is the engine's own aim cast, read live on the frame that uses
// it. It is never smoothed, rate-limited or replaced by a fixed convergence
// range: the reticle is glued to a surface, so when the aim crosses an edge the
// impact point genuinely jumps and the reticle has to jump with it, and a fixed
// d0 would sit on one side of the bullet hole inside that range and the other
// side beyond it.
//
// Residual, stated because it is real: on a miss the cast returns its own trace
// length rather than infinity, so a target beyond that range projects at the
// trace end. The error is lean * (1/range - 1/distance) - under half a degree
// at the leans this mod allows - and it shrinks as the target gets further. The
// range is not a constant this mod invented; it is whatever the game traced.
// The same projection the engine does, from a basis we hold rather than from
// whatever the camera object contains at this instant.
//
// The engine's world-to-screen reads the live camera, and the reticle's Update
// does not always run while that object holds the tracked transform - measured
// at 60/40 across 151 frames, and on the 40% the clean-aim point projects to
// dead centre, so the reticle stops compensating and rides along with the head.
// Forcing the accessor does not help, because on those frames the object itself
// is clean; it is an ordering problem, not a routing one.
//
// So the basis is supplied. CameraView's rows are right / down / forward with
// row 3 the position, and the scale mirrors the engine's own: a screen fraction
// with 0.5 at centre, y down, and x divided by the aspect
// (offset = tan(angle) / (2 * tanfov * aspect)).
void ProjectWithBasis(const float* basis, const float* point, float tanFov,
                      float aspect, float* outScreen) {
    const float dx = point[0] - basis[12];
    const float dy = point[1] - basis[13];
    const float dz = point[2] - basis[14];

    const float right   = dx * basis[0] + dy * basis[1]  + dz * basis[2];
    const float down    = dx * basis[4] + dy * basis[5]  + dz * basis[6];
    const float forward = dx * basis[8] + dy * basis[9]  + dz * basis[10];

    if (!(forward > 1.0f) || !(tanFov > 0.0f) || !(aspect > 0.0f)) {
        outScreen[0] = 0.5f;
        outScreen[1] = 0.5f;
        return;
    }
    outScreen[0] = 0.5f + (right / forward) / (2.0f * tanFov * aspect);
    outScreen[1] = 0.5f + (down  / forward) / (2.0f * tanFov);
}

bool AimScreenOffset(const CameraView& view, float aimMetres,
                    float& outX, float& outY) {
    const float* clean = view.clean;
    const float* tracked = view.tracked;

    // The impact point, in world units. 16-byte aligned because the engine
    // reads it with MOVAPS the moment it crosses the boundary.
    alignas(16) float point[4];
    float distance = (std::isfinite(aimMetres) && aimMetres > 0.0f)
                   ? aimMetres * s_unitsPerMetre
                   : 1000.0f * s_unitsPerMetre;   // no hit: a target at infinity
    for (int i = 0; i < 3; ++i) {
        point[i] = clean[12 + i] + clean[8 + i] * distance;
    }
    point[3] = 1.0f;

    // Reject when the aim has fallen behind the rendered view. The engine's
    // projection clamps its depth rather than failing, so it would happily
    // return a position for a point behind the camera.
    float d[3];
    for (int i = 0; i < 3; ++i) d[i] = point[i] - tracked[12 + i];
    float depth = d[0] * tracked[8] + d[1] * tracked[9] + d[2] * tracked[10];
    if (!(depth > 1.0f)) {
        return false;
    }

    // Our own projection, from the basis the camera hook captured for this
    // frame. Not the engine's world-to-screen: that reads whatever the camera
    // object holds at the instant it is called, and the reticle's Update does
    // not always run inside the window where that is the tracked transform.
    // Measured at 40% of frames on the clean transform, and on every one of
    // those the clean-aim point projected to dead centre, the offset collapsed
    // to zero, and the reticle sat in the middle of the screen being dragged
    // along by the head. That was the drift. Supplying the basis removes the
    // timing question instead of trying to win the race.
    float mine[2] = {0.5f, 0.5f};
    ProjectWithBasis(tracked, point, view.tanFov,
                     s_unitsPerScreenX / s_unitsPerScreenY, mine);

    // Once per session, prove the two still agree.
    //
    // Our projection has to encode the same basis layout, FOV convention,
    // handedness and range as the engine's, and all four are per-build facts.
    // The check waits for a frame the engine projected through the TRACKED
    // camera - identified by a point on the tracked forward axis landing at
    // centre - because on any other frame a disagreement would only be
    // measuring the timing fault above, not a real mismatch.
    if (!s_validated) {
        alignas(16) float trackedPoint[4];
        for (int i = 0; i < 3; ++i) {
            trackedPoint[i] = tracked[12 + i] + tracked[8 + i] * distance;
        }
        trackedPoint[3] = 1.0f;
        alignas(16) float trackedScreen[4] = {};
        s_engineProject(trackedScreen, trackedPoint);

        const bool engineUsedTracked = std::fabs(trackedScreen[0] - 0.5f) < 0.01f &&
                                       std::fabs(trackedScreen[1] - 0.5f) < 0.01f;
        if (engineUsedTracked) {
            alignas(16) float engineScreen[4] = {};
            s_engineProject(engineScreen, point);
            const float dx = std::fabs(engineScreen[0] - mine[0]);
            const float dy = std::fabs(engineScreen[1] - mine[1]);
            s_validated = true;
            if (dx > 0.002f || dy > 0.002f) {
                Log::Line("ReticleHook: WARNING - our projection disagrees with the engine's "
                          "by (%.4f %.4f) of the screen. The reticle will be placed wrongly; "
                          "the basis layout or the FOV convention has changed in this build.",
                          dx, dy);
            } else {
                Log::Line("ReticleHook: projection agrees with the engine's to "
                          "(%.4f %.4f) of the screen", dx, dy);
            }
        }
    }

    // 0..1 across the frame, so centre is 0.5 and the offset is what remains.
    //
    // From OUR projection, not the engine's. The two agree exactly whenever the
    // engine reads the tracked camera - verified live, to four decimals, on
    // combined yaw and pitch - but the engine's reads whatever the camera object
    // holds at this instant, and the reticle's Update does not always run inside
    // the window where that is the tracked transform. On the frames where it is
    // not, the clean-aim point projects to dead centre, the offset collapses to
    // zero, and the reticle stops compensating and is dragged along by the head.
    // Measured at 40% of frames. Supplying the basis removes the timing question
    // entirely rather than trying to win the race.
    outX = mine[0] - 0.5f;
    outY = mine[1] - 0.5f;
    return std::isfinite(outX) && std::isfinite(outY);
}

// Places the reticle for this frame, and reports where. Returns whether the
// screen position it wrote out is one the ADS marker may be drawn at; every path
// that does not produce one returns false, so the marker is decided fresh on
// every frame and can never be left standing where the rounds are not going.
bool PlaceReticle(void* self, float& outNdcX, float& outNdcY) {
    void* movie = *reinterpret_cast<void**>(static_cast<uint8_t*>(self) + s_movieOffset);
    if (movie == nullptr || !ResolveStage(movie)) {
        return false;
    }

    // A correction the movie ignores and a correction that is never computed
    // put the reticle in exactly the same place - where the game drew it - so
    // no amount of watching it in play tells the two apart. This sweeps the
    // reticle across most of the screen on a period of a few seconds, with no
    // tracker involved and ahead of every other gate in this function, so what
    // it answers is only ever "does the game put the reticle where this mod
    // asks". Wall clock rather than deltaTime because the units of that
    // argument are an assumption and the point of a probe is to make none.
    if (s_probe) {
        float phase = static_cast<float>(GetTickCount64() % 3142u) * 0.002f;
        float stageX = 0.4f * s_unitsPerScreenX * std::sin(phase);
        SetMovieNumber(movie, "_root._x", stageX);
        SetMovieNumber(movie, "_root._y", 0.0f);
        if (!s_loggedProbe) {
            s_loggedProbe = true;
            Log::Line("ReticleHook: PROBE active - sweeping the reticle +/-%.0f stage units "
                      "(40%% of the screen either side of centre). It is not tracking your "
                      "head. Set ReticleProbe=false to restore normal placement.",
                      0.4f * s_unitsPerScreenX);
        }
        return false;
    }

    const CameraView& view = CurrentCameraView();
    if (!view.valid) {
        return false;
    }

    // Live, on this frame, unsmoothed.
    float aimMetres = s_aimDistance(self, nullptr);

    float offsetX = 0.0f;
    float offsetY = 0.0f;
    bool onScreen = AimScreenOffset(view, aimMetres, offsetX, offsetY);

    // Off-screen leaves the reticle where the game put it rather than flinging
    // it to an arbitrary edge.
    float stageX = onScreen ? offsetX * s_unitsPerScreenX : 0.0f;
    float stageY = onScreen ? offsetY * s_unitsPerScreenY : 0.0f;

    SetMovieNumber(movie, "_root._x", stageX);
    SetMovieNumber(movie, "_root._y", stageY);

    ++s_frames;
    if (!s_loggedFirst) {
        s_loggedFirst = true;
        Log::Line("ReticleHook: moving the reticle movie (movie=%p)", movie);
    }

    // Every term of the projection on ONE line. Reading the rotation off one
    // line and the lean off another is what makes a distance fault and a sign
    // fault look alike, and it is the most expensive habit in this work.
    if (s_diag && (s_frames % kDiagInterval) == 0 && s_diagWritten < kMaxDiagLines) {
        ++s_diagWritten;
        // The angle between the clean and tracked forward axes, on the same
        // line as the offset it is supposed to produce. Without it an offset of
        // zero has two causes that read alike - the camera was never turned, or
        // it was turned and the projection was handed the clean camera anyway -
        // and they need opposite fixes.
        float dot = view.clean[8] * view.tracked[8] + view.clean[9] * view.tracked[9] +
                    view.clean[10] * view.tracked[10];
        if (dot > 1.0f) dot = 1.0f;
        if (dot < -1.0f) dot = -1.0f;
        float turnedDeg = std::acos(dot) * 57.2957795f;
        Log::Line("AIMGEO onscreen=%d dist=%.2fm rot=(%.2f %.2f %.2f) "
                  "lean=(%.3f %.3f %.3f) turned=%.2fdeg tanfov=%.4f "
                  "offset=(%.4f %.4f) stage=(%.1f %.1f)",
                  onScreen ? 1 : 0, aimMetres,
                  view.yaw, view.pitch, view.roll,
                  view.leanX, view.leanY, view.leanZ,
                  turnedDeg, view.tanFov,
                  offsetX, offsetY, stageX, stageY);
    }

    // Screen fractions from the centre, in the marker's own convention: -1..1
    // across the frame with y up, where the reticle's y runs down.
    outNdcX = offsetX * 2.0f;
    outNdcY = -offsetY * 2.0f;
    return onScreen;
}

void __fastcall ReticleUpdateDetour(void* self, void* edx, float deltaTime) {
    s_originalUpdate(self, edx, deltaTime);

    float ndcX = 0.0f;
    float ndcY = 0.0f;
    const bool placed = PlaceReticle(self, ndcX, ndcY);

    // The overlay comes up on the MODE, not on the sights: selecting the marker
    // mode is the player asking for a marker, and bringing it up then means it
    // is ready before the first aim rather than during one. A player who never
    // selects the mode never has their swap chain patched.
    const bool ready =
        s_tracking->GetAdsMode() == cameraunlock::ads::AdsMode::Marker && EnsureAimMarker();

    // Whether it DRAWS is derived per frame and never latched: only with the
    // sights up, and only for a projection that produced a position at all. A
    // marker mode whose overlay never came up behaves exactly like
    // AdsMode::Tracked.
    PublishAimMarker(ready && placed && SightsAreUp(), ndcX, ndcY);
}

}  // namespace

bool ReticleHook::Install(const BuildProfile& profile, const Config& cfg,
                          TrackingRuntime* tracking) {
    if (m_installed) return true;

    s_tracking = tracking;
    s_movieOffset = profile.reticleMovieOffset;
    s_setVariableVfunc = profile.movieSetVariableVfunc;
    s_unitsPerMetre = profile.unitsPerMetre;
    s_diag = cfg.camera_dump;
    s_probe = cfg.reticle_probe;

    HMODULE base = GetModuleHandleA(profile.module);
    if (!base) {
        Log::Line("ERROR: ReticleHook could not resolve the game module");
        return false;
    }
    uintptr_t moduleBase = reinterpret_cast<uintptr_t>(base);

    s_engineProject = reinterpret_cast<ProjectFn>(moduleBase + profile.worldToScreenRva);
    s_aimDistance = reinterpret_cast<AimDistanceFn>(moduleBase + profile.reticleAimRva);
    s_movieDimensions =
        reinterpret_cast<MovieDimensionsFn>(moduleBase + profile.movieDimensionsRva);
    s_visibleRectVfunc = profile.movieVisibleRectVfunc;

    void* target = reinterpret_cast<void*>(moduleBase + profile.reticleUpdateRva);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&ReticleUpdateDetour),
                      reinterpret_cast<void**>(&s_originalUpdate)) != MH_OK) {
        Log::Line("ERROR: MH_CreateHook(NsReticleMovieController::Update @ %p) failed", target);
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log::Line("ERROR: MH_EnableHook(reticle) failed");
        return false;
    }

    Log::Line("ReticleHook installed: target=%p movie+0x%X", target, s_movieOffset);
    m_installed = true;
    return true;
}

void ReticleHook::Uninstall() {
    m_installed = false;
}

}  // namespace DeusExHumanRevolutionHeadTracking

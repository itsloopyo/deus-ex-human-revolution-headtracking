#include "camera_hook.h"

#include "lean_trace.h"
#include "logging.h"

#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/camera/zoom_compensation.h"
#include "cameraunlock/math/rotation_utils.h"
#include "cameraunlock/math/vec3.h"

#include "MinHook.h"

#include <windows.h>

#include <atomic>
#include <intrin.h>
#include <cmath>
#include <cstdint>

namespace DeusExHumanRevolutionHeadTracking {

namespace {

// CameraManager::Update is thiscall (this in ECX, no other args). The x86
// MinHook idiom is a __fastcall detour: arg0 lands in ECX (= this), arg1 in
// EDX (unused). Both conventions pop zero stack args, so the trampoline call
// stays balanced.
using UpdateFn = void(__fastcall*)(void* self, void* edx);


// The camera accessors are thiscall too, and take no arguments.
using GetterFn = void*(__fastcall*)(void* self, void* edx);

// The camera's own GetFov, thiscall with no arguments, returning radians on the
// x87 stack. Called rather than read out of the object because it is virtual:
// each camera class answers for itself, and the clamps it applies are the ones
// the engine's own tan() sees.
using GetFovFn = float(__fastcall*)(void* self, void* edx);

UpdateFn         s_original = nullptr;
TrackingRuntime* s_tracking = nullptr;
uint32_t         s_matrixOffset = 0;
uint32_t         s_activeCameraOffset = 0;
uint32_t         s_matrixVfunc = 0;
uint32_t         s_anglesOffset = 0;
uint32_t         s_worldOffset = 0;
uint32_t         s_worldVfunc = 0;
uint32_t         s_fovOffset = 0;
uint32_t         s_fovVfunc = 0;
const float*     s_aspect = nullptr;
float            s_baseTanHalfFov = 0.0f;
const uint32_t*  s_renderReads = nullptr;
int              s_renderReadCount = 0;

// The CameraManager, captured from its own Update so the render hook can reach
// the active camera without repeating the engine's lookup.
void* s_cameraManager = nullptr;

// The frame's clean/tracked pair, published for the reticle.
CameraView s_view = {};
bool  s_loggedDecouple = false;
bool  s_haveClean = false;
DWORD s_gameThreadId = 0;

// The camera left injected at the end of the render view setup, put back at the
// top of the next camera update. The render's own camera read happens somewhere
// after the setup call rather than inside it, so the pose has to outlive the
// call; restoring at the next update still leaves every gameplay tick that runs
// before the setup - aim, interaction, AI - reading a clean camera.
void* s_injectedCamera = nullptr;

float            s_unitsPerMetre = 0.0f;
bool             s_diag = false;

// Cuts the lean down to whatever the level leaves room for. Stateful - it damps
// the release, so it is reset whenever the pose stops arriving and a wall the
// player was leaning on before a load screen cannot ration the next lean.
cameraunlock::camera::LeanClamp s_leanClamp;
bool  s_leanCollision = false;
float s_leanSkinMetres = 0.0f;

std::atomic<uint64_t> s_callCount{0};

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

// ---- Camera layout ---------------------------------------------------------
//
// Derived by inspecting the shipped DXHRDC.exe and a live camera dump, not
// guessed. Offsets are facts about the layout, recorded as numbers.
//
// The active camera object holds its orientation three times over, and only
// the first two are upstream of the picture:
//
//   +0x10  position vec4
//   +0x20  pitch, roll, yaw in radians (world heading = yaw + pi/2)
//   +0x40  camera-to-world transform: ROWS 0-2 are the right/down/forward
//          basis, row 3 is the world position
//   +0x80  the view matrix, transposed out of +0x40, whose COLUMNS are the
//          basis and whose row 3 is -position projected onto them
//
// CameraManager::Update copies +0x80 into CameraManager+0x13B0, FOV-scales two
// of its columns and rebuilds its row 3. Writing head tracking into +0x80 or
// into that copy moves nothing on screen: both are leaves of the derivation,
// so the pose has to go into the angles and the world transform.
//
// The world is right-handed with +Z up; view space is y-down, which is why the
// middle basis vector is "down" rather than "up".
constexpr int   kAxisRight = 0;
constexpr int   kAxisDown  = 1;
constexpr int   kAxisFwd   = 2;
constexpr float kWorldUp[3] = {0.0f, 0.0f, 1.0f};

// The camera axes pulled out of a transform: unit direction plus any scale the
// engine baked in, so the scale survives the round trip untouched.
struct Basis {
    float axis[3][3];
    float scale[3];
};

// Everything we overwrite, kept so the game's own camera code never sees - and
// never integrates - our rotation. Restored at the top of the next Update.
// The engine loads this transform with MOVAPS, an ALIGNED 16-byte SSE read, so
// the copy we hand back to callers has to satisfy the same alignment the real
// one does. A 4-byte-misaligned buffer faults the moment the game touches it.
struct CameraSnapshot {
    alignas(16) float world[16];
    float angles[3];
};

CameraSnapshot s_clean = {};

bool s_loggedInjecting = false;
bool s_loggedBadBasis = false;
bool s_loggedBadFov = false;
bool s_loggedZoom = false;
bool s_loggedFovBasis = false;

// A transform stores its basis either along the rows (camera-to-world) or down
// the columns (the transposed view matrix).
bool ReadBasis(const float* m, bool axesInRows, Basis& out) {
    for (int j = 0; j < 3; ++j) {
        if (axesInRows) {
            out.axis[j][0] = m[j * 4 + 0];
            out.axis[j][1] = m[j * 4 + 1];
            out.axis[j][2] = m[j * 4 + 2];
        } else {
            out.axis[j][0] = m[j];
            out.axis[j][1] = m[4 + j];
            out.axis[j][2] = m[8 + j];
        }
        out.scale[j] = cameraunlock::math::Normalize3(out.axis[j]);
        if (!std::isfinite(out.scale[j]) || out.scale[j] < 1e-3f || out.scale[j] > 1e3f) {
            return false;
        }
    }
    for (int i = 12; i < 15; ++i) {
        if (!std::isfinite(m[i])) return false;
    }
    // A FOV scale is applied per axis, so it cannot cost the axes their mutual
    // orthogonality. Losing that means this is not the transform we think.
    constexpr float kMaxAxisDot = 0.05f;
    return std::fabs(cameraunlock::math::Dot3(out.axis[0], out.axis[1])) < kMaxAxisDot &&
           std::fabs(cameraunlock::math::Dot3(out.axis[0], out.axis[2])) < kMaxAxisDot &&
           std::fabs(cameraunlock::math::Dot3(out.axis[1], out.axis[2])) < kMaxAxisDot;
}

void WriteBasis(float* m, bool axesInRows, const Basis& b) {
    for (int j = 0; j < 3; ++j) {
        if (axesInRows) {
            m[j * 4 + 0] = b.axis[j][0] * b.scale[j];
            m[j * 4 + 1] = b.axis[j][1] * b.scale[j];
            m[j * 4 + 2] = b.axis[j][2] * b.scale[j];
        } else {
            m[j]     = b.axis[j][0] * b.scale[j];
            m[4 + j] = b.axis[j][1] * b.scale[j];
            m[8 + j] = b.axis[j][2] * b.scale[j];
        }
    }
}

// Rotation is linear and norm-preserving, so any baked scale is unaffected and
// only the unit directions move.
void RotateBasis(Basis& b, const float axis[3], float angleRad) {
    float c = std::cos(angleRad);
    float s = std::sin(angleRad);
    for (int j = 0; j < 3; ++j) {
        float out[3];
        cameraunlock::math::RotateAroundAxis(b.axis[j], axis, c, s, out);
        b.axis[j][0] = out[0];
        b.axis[j][1] = out[1];
        b.axis[j][2] = out[2];
    }
}

void ApplyHeadRotation(Basis& b, float yawDeg, float pitchDeg, float rollDeg,
                       bool worldSpaceYaw) {
    // Tracker yaw and roll arrive mirrored relative to the engine's positive
    // rotation directions, so both are negated here, once, at the boundary
    // where the tracker convention meets the engine's. Pitch matches already:
    // a positive tracker pitch rotates the forward axis towards up.
    float axis[3];

    // World-space (horizon-locked) yaw turns the basis about the world up axis,
    // so yaw direction stays independent of where the camera is pitched.
    // Camera-local yaw turns about the camera's own up axis instead, which
    // leans the view at extreme pitches.
    if (worldSpaceYaw) {
        axis[0] = kWorldUp[0];
        axis[1] = kWorldUp[1];
        axis[2] = kWorldUp[2];
    } else {
        axis[0] = -b.axis[kAxisDown][0];
        axis[1] = -b.axis[kAxisDown][1];
        axis[2] = -b.axis[kAxisDown][2];
    }
    RotateBasis(b, axis, -yawDeg * kDegToRad);

    // Pitch about the post-yaw right axis, then roll about the post-pitch
    // forward axis. Both axes must be copied out first: RotateBasis rewrites
    // every axis in the basis, including the one it is turning around.
    axis[0] = b.axis[kAxisRight][0];
    axis[1] = b.axis[kAxisRight][1];
    axis[2] = b.axis[kAxisRight][2];
    RotateBasis(b, axis, pitchDeg * kDegToRad);

    axis[0] = b.axis[kAxisFwd][0];
    axis[1] = b.axis[kAxisFwd][1];
    axis[2] = b.axis[kAxisFwd][2];
    RotateBasis(b, axis, -rollDeg * kDegToRad);
}

// Head position, in metres, resolved against the CLEAN camera axes so the
// offset follows the body's orientation rather than the head-rotated view.
// The tracker's x and z arrive mirrored, and its negative z is the forward
// lean, so both are flipped once here - at the same boundary as the rotation
// signs above, and before any directional limit is applied.
void HeadPositionDelta(const Basis& clean, const HeadPose& pose, float outDelta[3]) {
    float right = -pose.x * s_unitsPerMetre;
    float up    =  pose.y * s_unitsPerMetre;
    float fwd   = -pose.z * s_unitsPerMetre;
    for (int i = 0; i < 3; ++i) {
        outDelta[i] = right * clean.axis[kAxisRight][i]
                    - up    * clean.axis[kAxisDown][i]
                    + fwd   * clean.axis[kAxisFwd][i];
    }
}

// How much the field of view the game is rendering with magnifies the picture,
// as the factor the head pose has to be scaled by to move the image as far as
// it does when nothing is zoomed.
//
// Popping out of cover, raising the sights and putting a scope up all narrow
// the FOV, and a narrow FOV carries everything on screen further for the same
// camera rotation - 2.4x at this game's 45 degree scope against its 90 degree
// walking-around view. Left alone the head turns the camera by its own angle
// either way and the player reads the magnification as the mod's sensitivity
// jumping the moment they aim.
//
// Exactly 1.0 whenever the game is at its own default FOV, which is where it
// sits for nearly all of a session, so this changes nothing in normal play.
// It is keyed to the FOV being rendered rather than to the aim state, so a
// vision aug or a cinematic pull-in gets the same treatment as the sights.
// GetFov answers VERTICALLY while the game's fields of view are authored
// horizontally, so the live tangent is carried across by the aspect before it is
// compared with the base. Getting that wrong does not look like a bug: the two
// numbers are both fields of view, both in radians, and the ratio between them
// is simply a constant, so the whole of normal play runs at a fixed fraction of
// the pose and the only symptom is head tracking feeling weak everywhere.
float ZoomFactor(void* camera) {
    uint8_t* vtable = *reinterpret_cast<uint8_t**>(camera);
    GetFovFn getFov = *reinterpret_cast<GetFovFn*>(vtable + s_fovVfunc);
    const float fov = getFov(camera, nullptr);
    const float aspect = *s_aspect;

    // Both numbers come out of the running process, so this is the boundary
    // check. An unreadable FOV means no compensation, never a guessed one.
    if (!std::isfinite(fov) || fov <= 0.0f || fov >= kPi ||
        !std::isfinite(aspect) || aspect <= 0.1f || aspect >= 10.0f) {
        if (!s_loggedBadFov) {
            s_loggedBadFov = true;
            Log::Line("WARN: the camera reports a %.4f rad vertical field of view at an "
                      "aspect of %.4f. Head tracking is not being scaled for zoom, so it "
                      "will feel stronger through sights and scopes.",
                      fov, aspect);
        }
        return 1.0f;
    }

    const float tanHalfHorizontal = std::tan(fov * 0.5f) * aspect;
    const float factor =
        cameraunlock::camera::FovZoomFactor(tanHalfHorizontal, s_baseTanHalfFov);

    // Once, with every term on the line, because a factor that is wrong by a
    // constant reads exactly like a factor that is right - the whole of normal
    // play just runs at a fixed fraction of the pose, and head tracking feels
    // weak everywhere rather than wrong anywhere. The line has to say 1.0000
    // against the base FOV in ordinary play; anything else is this fault.
    if (!s_loggedZoom) {
        s_loggedZoom = true;
        Log::Line("CameraHook: field of view %.2f deg vertical at aspect %.4f = %.2f deg "
                  "horizontal, against a %.2f deg base; head tracking scaled by %.4f",
                  fov / kDegToRad, aspect,
                  2.0f * std::atan(tanHalfHorizontal) / kDegToRad,
                  2.0f * std::atan(s_baseTanHalfFov) / kDegToRad, factor);
    }
    return factor;
}

// The pose as it will actually be applied.
//
// Yaw and pitch translate the image across the frame and both scale. A lean
// translates it too, linearly, so position scales with them. Roll ROTATES the
// image about the view axis - ten degrees of head roll rolls the picture ten
// degrees at every field of view there is - so roll is left alone.
HeadPose ScaleForZoom(const HeadPose& pose, float zoom) {
    using cameraunlock::camera::ScaleAngleForZoom;
    HeadPose out = pose;
    out.yaw = ScaleAngleForZoom(pose.yaw, zoom);
    out.pitch = ScaleAngleForZoom(pose.pitch, zoom);
    out.x = pose.x * zoom;
    out.y = pose.y * zoom;
    out.z = pose.z * zoom;
    return out;
}

// Which object each camera read actually lands on. The render thread consumes
// the camera asynchronously, so the only safe thing to inject into is whatever
// object the render snapshot is taken from - and that may not be the active
// camera we have been writing to. Logged with the caller so the two can be told
// apart.
using AccessorFn = void*(__fastcall*)(void* self, void* edx);
AccessorFn s_origAccessor = nullptr;
void**     s_patchedSlot = nullptr;
uintptr_t  s_moduleBase = 0;


// Every distinct call site that reads the camera, logged once with what it was
// served. The render-read list is an allow-list, so a projection path nobody
// enumerated is served the clean camera and draws where the player is aiming
// rather than where they are looking. That is invisible until something
// world-anchored fails to line up, and guessing at which call sites exist is
// what this replaces. Capped, and a race can only cost a duplicate line.
constexpr int kMaxCallers = 64;
std::atomic<uint32_t> s_callerRvas[kMaxCallers];
std::atomic<int>      s_callerCount{0};

void NoteCameraRead(uint32_t rva, bool tracked) {
    int n = s_callerCount.load(std::memory_order_acquire);
    if (n > kMaxCallers) n = kMaxCallers;
    for (int i = 0; i < n; ++i) {
        if (s_callerRvas[i].load(std::memory_order_relaxed) == rva) return;
    }
    int slot = s_callerCount.fetch_add(1, std::memory_order_acq_rel);
    if (slot >= kMaxCallers) return;
    s_callerRvas[slot].store(rva, std::memory_order_release);
    Log::Line("CAMREAD rva=0x%08X served=%s", rva, tracked ? "tracked" : "clean");
}

bool IsRenderRead(uintptr_t caller) {
    uint32_t rva = static_cast<uint32_t>(caller - s_moduleBase);
    for (int i = 0; i < s_renderReadCount; ++i) {
        if (s_renderReads[i] == rva) return true;
    }
    return false;
}

// The camera object stays head-tracked, and this is where the two audiences are
// told apart. The reads that build the rendered view get it as it stands; aim,
// interaction, AI, the HUD and audio are handed the clean transform the player
// is actually pointing with. Splitting on the call site rather than the clock
// means there is no window to race. A read from any other thread is left alone,
// since the render thread is the one consumer that must never be given clean.
void* __fastcall AccessorDetour(void* self, void* edx) {
    void* live = s_origAccessor(self, edx);
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());

    bool foreign = (self != s_injectedCamera) || !s_haveClean ||
                   GetCurrentThreadId() != s_gameThreadId;
    bool tracked = foreign || IsRenderRead(caller);
    if (s_diag && !foreign) {
        NoteCameraRead(static_cast<uint32_t>(caller - s_moduleBase), tracked);
    }
    if (tracked) {
        return live;
    }

    if (!s_loggedDecouple) {
        s_loggedDecouple = true;
        Log::Line("CameraHook: decoupled - render reads keep the tracked camera, "
                  "game reads get the clean one");
    }
    return s_clean.world;
}

void PatchAccessor(void* camera) {
    if (s_patchedSlot != nullptr) return;
    void** vtable = *reinterpret_cast<void***>(camera);
    void** slot = reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(vtable) + s_worldVfunc);
    DWORD prot = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &prot)) return;
    s_origAccessor = reinterpret_cast<AccessorFn>(*slot);
    *slot = reinterpret_cast<void*>(&AccessorDetour);
    VirtualProtect(slot, sizeof(void*), prot, &prot);
    s_patchedSlot = slot;
    Log::Line("CameraHook: injecting into camera object=%p (vtable=%p, slot %p)",
              camera, vtable, slot);
}

constexpr int kDiagInterval = 300;   // ~5s at 60fps
constexpr int kMaxDiagLines = 40;
int s_diagWritten = 0;

void* ActiveCamera(void* self) {
    return *reinterpret_cast<void**>(static_cast<uint8_t*>(self) + s_activeCameraOffset);
}

// The camera's own account of what it is rendering, logged on the first frame
// the camera updates at all rather than on the first frame a pose arrives.
// Without it the whole FOV basis stays invisible until a tracker is connected
// and a save is loaded, which is the wrong moment to discover that a patch moved
// the aspect.
void LogFovBasisOnce(void* cameraManager) {
    if (s_loggedFovBasis) return;
    void* camera = ActiveCamera(cameraManager);
    if (camera == nullptr) return;
    s_loggedFovBasis = true;

    uint8_t* vtable = *reinterpret_cast<uint8_t**>(camera);
    GetFovFn getFov = *reinterpret_cast<GetFovFn*>(vtable + s_fovVfunc);
    Log::Line("CameraHook: the camera reports %.2f deg vertical at aspect %.4f",
              getFov(camera, nullptr) / kDegToRad, *s_aspect);
}

float* CameraField(void* camera, uint32_t offset) {
    return reinterpret_cast<float*>(static_cast<uint8_t*>(camera) + offset);
}

// Puts the camera back the way the game left it. Anything reading the camera
// from here until the next injection - the game's own camera update, aim,
// raycasts - sees no head tracking at all.
void RestoreCamera(void* camera) {
    float* angles = CameraField(camera, s_anglesOffset);
    for (int i = 0; i < 3; ++i) angles[i] = s_clean.angles[i];
    float* world = CameraField(camera, s_worldOffset);
    for (int i = 0; i < 16; ++i) world[i] = s_clean.world[i];
}

// Contact and a failed query both read to the player as "leaning stopped
// working", so both are logged, and only on the edge - a per-frame line at 60fps
// would bury everything else in the log.
void LogLeanClamp(float wanted, float allowed) {
    static bool loggedFailure = false;
    const bool failed = s_leanClamp.LastQueryFailed();
    if (failed != loggedFailure) {
        loggedFailure = failed;
        if (failed) {
            Log::Line("lean-clamp: the world query is not answering; the lean is "
                      "running unclamped");
        } else {
            Log::Line("lean-clamp: the world query is answering again");
        }
    }

    static bool loggedContact = false;
    const bool contact = s_leanClamp.InContact();
    if (contact == loggedContact) return;
    loggedContact = contact;
    if (contact) {
        Log::Line("lean-clamp: the eye is held off a surface (wanted %.1fcm, "
                  "allowed %.1fcm)",
                  wanted / s_unitsPerMetre * 100.0f, allowed / s_unitsPerMetre * 100.0f);
    } else {
        Log::Line("lean-clamp: clear of the surface, the lean is free again");
    }
}

// Holds the eye short of whatever the level puts in front of it. Everything the
// game reads is upstream of this, so a shortened lean changes what the player
// sees and nothing else: aim, projectiles and the interaction pick all still run
// off the clean camera this was measured from.
// How far past the near clip plane the standoff has to sit. Stopping the eye
// exactly ON the near plane still cuts: the plane is perpendicular to the view
// while a wall rarely is, so an angled surface has points nearer than the
// distance measured along the lean, and the collision hull a trace hits sits
// behind whatever trim, pipework and door frame the renderer draws in front of
// it. Half the near distance again covers both.
constexpr float kNearPlaneMargin = 1.5f;

// The standoff to hold this frame, in world units: what the player asked for, or
// the near clip plane with its margin, whichever is further off the wall.
//
// The floor is not a preference being overruled for its own sake. Below it the
// setting cannot do what its name says - geometry closer to the eye than the
// near plane is never drawn, so an eye stopped short of a wall but still inside
// that plane looks straight through it, and the wall opens into a polygonal
// cutaway. A skin that lands there is a clamp that runs perfectly and shows the
// player the exact artefact it exists to prevent.
float EffectiveSkinUnits() {
    const float asked = s_leanSkinMetres * s_unitsPerMetre;
    const float nearPlane = lean_trace::NearPlaneUnits();
    if (nearPlane <= 0.0f) return asked;

    const float floorUnits = nearPlane * kNearPlaneMargin;
    if (asked >= floorUnits) return asked;

    static bool logged = false;
    if (!logged) {
        logged = true;
        Log::Line("lean-clamp: LeanCollisionSkin=%.2fm is inside this camera's near "
                  "clip plane (%.0f units, %.3fm), where a held-off wall is still not "
                  "drawn. Holding the eye at %.3fm instead.",
                  s_leanSkinMetres, nearPlane, nearPlane / s_unitsPerMetre,
                  floorUnits / s_unitsPerMetre);
    }
    return floorUnits;
}

void ClampLeanToWorld(const float* world, float delta[3], float dt) {
    using cameraunlock::math::Vec3;

    cameraunlock::camera::LeanClampSettings settings;
    settings.skin = EffectiveSkinUnits();
    s_leanClamp.SetSettings(settings);

    // The engine reads the trace's start point as a whole vec4, and that point is
    // the transform's position row, so it goes back with the w the game put in it.
    lean_trace::SetEyeW(world[15]);

    const Vec3 eye(world[12], world[13], world[14]);
    const Vec3 wanted(delta[0], delta[1], delta[2]);
    const Vec3 allowed =
        s_leanClamp.Apply(eye, wanted, dt, &lean_trace::Query, nullptr);

    delta[0] = allowed.x;
    delta[1] = allowed.y;
    delta[2] = allowed.z;
    LogLeanClamp(wanted.Magnitude(), allowed.Magnitude());
}

void InjectCamera(void* camera, const HeadPose& pose, bool worldSpaceYaw, float dt) {
    float* world = CameraField(camera, s_worldOffset);

    Basis basis;
    if (!ReadBasis(world, true, basis)) {
        if (!s_loggedBadBasis) {
            s_loggedBadBasis = true;
            Log::Line("WARN: the camera transform at +0x%X has no orthogonal basis; head "
                      "tracking not applied. Set CameraDump=true to inspect it.",
                      s_worldOffset);
        }
        return;
    }

    for (int i = 0; i < 3; ++i) s_clean.angles[i] = CameraField(camera, s_anglesOffset)[i];
    for (int i = 0; i < 16; ++i) s_clean.world[i] = world[i];

    if (pose.position_valid) {
        float delta[3];
        HeadPositionDelta(basis, pose, delta);
        if (s_leanCollision) {
            ClampLeanToWorld(world, delta, dt);
        }
        for (int i = 0; i < 3; ++i) world[12 + i] += delta[i];
    }

    if (pose.rotation_valid) {
        ApplyHeadRotation(basis, pose.yaw, pose.pitch, pose.roll, worldSpaceYaw);
        WriteBasis(world, true, basis);
    }
}

void __fastcall UpdateDetour(void* self, void* edx) {
    s_cameraManager = self;
    s_gameThreadId = GetCurrentThreadId();

    // The game rebuilds the camera transform from the player's own look angles
    // here, so it always starts from clean and our rotation can never compound.
    s_original(self, edx);

    LogFovBasisOnce(self);

    HeadPose pose;
    if (!s_tracking->SamplePerFrame(pose)) {
        // Drop the allowance too: a wall the player was leaning on before a menu
        // or a load screen must not ration the first lean of the next scene
        // through the clamp's release ease.
        s_leanClamp.Reset();
        return;
    }

    void* camera = ActiveCamera(self);
    if (camera == nullptr) {
        return;
    }

    bool worldSpaceYaw = s_tracking->IsWorldSpaceYaw();
    if (!s_loggedInjecting) {
        s_loggedInjecting = true;
        Log::Line("CameraHook: injecting head tracking (%s yaw)",
                  worldSpaceYaw ? "world-space" : "camera-local");
    }

    // Everything below this line works from the scaled pose, so the injection,
    // the lean clamp, the reticle's basis and the diagnostics all describe the
    // same camera.
    const float zoom = ZoomFactor(camera);
    const HeadPose applied = ScaleForZoom(pose, zoom);

    PatchAccessor(camera);
    InjectCamera(camera, applied, worldSpaceYaw, s_tracking->LastFrameDt());
    s_injectedCamera = camera;
    s_haveClean = true;

    const float* world = CameraField(camera, s_worldOffset);
    for (int i = 0; i < 16; ++i) {
        s_view.clean[i] = s_clean.world[i];
        s_view.tracked[i] = world[i];
    }
    s_view.tanFov = *reinterpret_cast<const float*>(
        static_cast<const uint8_t*>(self) + s_fovOffset);
    s_view.cameraManager = self;
    s_view.yaw = applied.yaw;
    s_view.pitch = applied.pitch;
    s_view.roll = applied.roll;
    s_view.leanX = applied.x;
    s_view.leanY = applied.y;
    s_view.leanZ = applied.z;
    s_view.valid = true;

    uint64_t n = s_callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (s_diag && (n % kDiagInterval) == 0 && s_diagWritten < kMaxDiagLines) {
        ++s_diagWritten;
        Log::Line("CAM #%llu pose yaw=%.2f pitch=%.2f roll=%.2f pos=(%.3f %.3f %.3f)m "
                  "zoom=%.3f applied yaw=%.2f pitch=%.2f pos=(%.3f %.3f %.3f)m "
                  "fwd=(%.4f %.4f %.4f) clean=(%.4f %.4f %.4f)",
                  static_cast<unsigned long long>(n), pose.yaw, pose.pitch, pose.roll,
                  pose.x, pose.y, pose.z,
                  zoom, applied.yaw, applied.pitch, applied.x, applied.y, applied.z,
                  world[8], world[9], world[10],
                  s_clean.world[8], s_clean.world[9], s_clean.world[10]);
    }
}

}  // namespace

const CameraView& CurrentCameraView() { return s_view; }

bool CameraHook::Install(const BuildProfile& profile, const Config& cfg, TrackingRuntime* tracking) {
    if (m_installed) return true;

    m_profile = &profile;
    s_tracking = tracking;
    s_matrixOffset = profile.cameraMatrixOffset;
    s_activeCameraOffset = profile.activeCameraOffset;
    s_matrixVfunc = profile.cameraMatrixVfunc;
    s_anglesOffset = profile.cameraAnglesOffset;
    s_worldOffset = profile.cameraWorldOffset;
    s_worldVfunc = profile.cameraWorldVfunc;
    s_fovOffset = profile.cameraFovOffset;
    s_fovVfunc = profile.cameraFovVfunc;
    s_renderReads = profile.renderReadRvas;
    s_renderReadCount = profile.renderReadCount;
    s_unitsPerMetre = profile.unitsPerMetre;
    s_leanCollision = cfg.lean_collision;
    s_leanSkinMetres = cfg.lean_collision_skin_m;
    s_diag = cfg.camera_dump;

    HMODULE base = GetModuleHandleA(profile.module);  // null -> the EXE
    s_moduleBase = reinterpret_cast<uintptr_t>(base);
    if (!base) {
        Log::Line("ERROR: GetModuleHandle(%s) failed", profile.module ? profile.module : "<exe>");
        return false;
    }
    s_aspect = reinterpret_cast<const float*>(
        reinterpret_cast<uintptr_t>(base) + profile.aspectRva);
    s_baseTanHalfFov = std::tan(profile.baseHorizontalFovDeg * 0.5f * kDegToRad);

    void* target = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(base) + profile.cameraUpdateRva);

    if (MH_CreateHook(target, reinterpret_cast<void*>(&UpdateDetour),
                      reinterpret_cast<void**>(&s_original)) != MH_OK) {
        Log::Line("ERROR: MH_CreateHook(CameraManager::Update @ %p) failed", target);
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log::Line("ERROR: MH_EnableHook failed");
        return false;
    }

    // Which graphics DLLs happen to be mapped at hook-install time, which is a
    // fraction of a second into the process. It is NOT which renderer the game
    // uses, and it must never be read as one: this line said d3d9 and not d3d11
    // on a build that renders through Direct3D 11 throughout, and an overlay was
    // written against the wrong API on the strength of it. To answer the
    // renderer question, enumerate the modules of the running process once it is
    // drawing.
    Log::Line("Graphics DLLs mapped at install time (NOT the renderer): "
              "d3d11=%d dxgi=%d d3d9=%d",
              GetModuleHandleA("d3d11.dll") ? 1 : 0,
              GetModuleHandleA("dxgi.dll") ? 1 : 0,
              GetModuleHandleA("d3d9.dll") ? 1 : 0);

    lean_trace::Install(profile);

    Log::Line("CameraHook installed: profile=%s target=%p camera+0x%X/0x%X units/m=%.2f "
              "lean-collision=%s skin=%.2fm base-fov=%.1fdeg-horizontal diag=%d",
              profile.name, target, s_anglesOffset, s_worldOffset, s_unitsPerMetre,
              s_leanCollision ? "on" : "off", s_leanSkinMetres,
              profile.baseHorizontalFovDeg, s_diag ? 1 : 0);
    m_installed = true;
    return true;
}

void CameraHook::Uninstall() {
    m_installed = false;
}

}  // namespace DeusExHumanRevolutionHeadTracking

#pragma once

#include "build_profile.h"
#include "config.h"
#include "tracking_runtime.h"

namespace DeusExHumanRevolutionHeadTracking {

// Hooks CameraManager::Update to turn the active camera's transform by the head
// pose (6DOF, world-space or camera-local yaw per
// TrackingRuntime::IsWorldSpaceYaw).
//
// The camera object stays head-tracked, and the split between the two audiences
// happens in the transform accessor: the reads that build the rendered view get
// the tracked transform, while aim, interaction, AI and audio are handed the
// clean one. Decoupling by call site rather than by timing leaves nothing to
// race against the engine's render thread.
// The two cameras of the current frame, as camera-to-world transforms whose
// rows 0-2 are the right/down/forward basis and whose row 3 is the position.
// `clean` is where the player is aiming, `tracked` is what they are looking
// through; the reticle lives at the projection of the first into the second.
struct CameraView {
    bool  valid;
    float clean[16];
    float tracked[16];
    float tanFov;      // tan(fov/2) as the engine's own projections use it
    void* cameraManager;
    // The pose that produced the pair, so the reticle can log rotation and lean
    // on the same line as the projection it computed from them.
    float yaw, pitch, roll;
    float leanX, leanY, leanZ;
};

const CameraView& CurrentCameraView();

// MinHook is initialised by the caller before any of these Install() calls, and
// torn down after their Uninstall()s: it is process-wide, and four hooks in this
// mod share it now.
class CameraHook {
public:
    bool Install(const BuildProfile& profile, const Config& cfg, TrackingRuntime* tracking);
    void Uninstall();

private:
    const BuildProfile* m_profile = nullptr;
    bool m_installed = false;
};

}  // namespace DeusExHumanRevolutionHeadTracking

#pragma once

#include "cameraunlock/ads/ads_blend.h"
#include "cameraunlock/ads/ads_fade.h"
#include "cameraunlock/ads/ads_mode.h"
#include "cameraunlock/ads/entry_pose.h"

namespace DeusExHumanRevolutionHeadTracking {

// What the head pose becomes while the sights are up: the transition, the
// entry-relative pose and the blend between them, in one place.
//
// Separate from TrackingRuntime because everything here is decidable without a
// socket, a camera or a game, and none of it is visible from a settings test.
// The seam-crossing yaw, the capture that has to wait for a live rotation, and a
// reversal that starts from where the transition actually is are all frames a
// player either sees their head jump on or does not.
//
// This mod keeps feeding the camera through the aim rather than handing it back:
// the camera hook writes whatever pose comes out of here every frame, so
// AdsMode::Paused is a pose faded to nothing rather than a gate that shuts. That
// is why nothing here has to hold a gate open for the length of the transition.
class AdsPipeline {
public:
    using Pose = cameraunlock::ads::AdsEntryPose::Pose;

    // `aiming` is the game's own sight state, polled - never this mod's own
    // verdict about whether tracking applies. In Paused the fade is what takes
    // the pose away, so feeding a verdict back in would make the fade restart
    // itself several times a second for as long as the trigger is held.
    //
    // `live` says this frame's rotation is a real sample rather than the nothing
    // a suppressed frame publishes, which is what stops the entry pose being
    // captured from a stale interpolator.
    Pose Apply(cameraunlock::ads::AdsMode mode, bool aiming, bool live,
               const Pose& absolute, unsigned long long nowMs) {
        const Pose relative = m_entry.Relative(aiming, live, absolute);
        const float scale = m_fade.Update(aiming, nowMs);
        return cameraunlock::ads::BlendAdsPose(mode, scale, absolute, relative);
    }

    // Every reason tracking stands down - the master toggle, a stale tracker, no
    // pose at all - so the next aim re-enters against the head where it actually
    // is rather than against a pose from before the gap.
    void Suppress() {
        m_fade.Reset();
        m_entry.Reset();
    }

private:
    cameraunlock::ads::AdsFade m_fade;
    cameraunlock::ads::AdsEntryPose m_entry;
};

}  // namespace DeusExHumanRevolutionHeadTracking

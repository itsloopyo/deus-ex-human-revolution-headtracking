#pragma once

#include "cameraunlock/ads/ads_fade.h"

namespace DeusExHumanRevolutionHeadTracking {

// Eases the lean out while the sights are up and back in when they come down.
//
// Raising the sights puts the weapon's sight line through the eye, and head
// rotation turns the view about that same eye, so the sights stay lined up with
// the head turned and rotation passes through untouched. A lean translates the
// eye off that line, and this mod does not own a weapon pass it could draw from
// the clean eye instead, so the lean is scaled by core's AdsFade: 150ms out,
// 250ms back, a reversal continuing from where the transition is.
//
// Separate from TrackingRuntime because it is decidable without a socket, a
// camera or a game, which is what lets the tests drive it frame by frame.
class AdsLean {
public:
    struct Pose {
        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    // `aiming` is the game's own sight state, polled this frame.
    Pose Apply(bool aiming, const Pose& pose, unsigned long long nowMs) {
        const float scale = m_fade.Update(aiming, nowMs);
        Pose out = pose;
        out.x *= scale;
        out.y *= scale;
        out.z *= scale;
        return out;
    }

    // Every reason tracking stands down - the master toggle, a stale tracker, no
    // pose at all - so the next frame starts from the hip rather than from a
    // transition left over from before the gap.
    void Suppress() { m_fade.Reset(); }

private:
    cameraunlock::ads::AdsFade m_fade;
};

}  // namespace DeusExHumanRevolutionHeadTracking

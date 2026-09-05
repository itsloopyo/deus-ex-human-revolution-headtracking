#include "ads_pose.h"

#include <cmath>
#include <cstdio>

using namespace DeusExHumanRevolutionHeadTracking;
using cameraunlock::ads::AdsFade;
using cameraunlock::ads::AdsMode;

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

void Near(float got, float want, float tol, const char* what) {
    if (!(std::fabs(got - want) <= tol)) {
        std::printf("  FAIL: %s (got %.4f, wanted %.4f)\n", what, got, want);
        ++g_failures;
    }
}

AdsPipeline::Pose Pose(float yaw, float pitch, float roll,
                       float x = 0.0f, float y = 0.0f, float z = 0.0f) {
    AdsPipeline::Pose p;
    p.yaw = yaw;
    p.pitch = pitch;
    p.roll = roll;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

// Long enough that a transition started at t0 has finished, whichever direction
// it was going.
constexpr unsigned long long kSettled = AdsFade::kLowerMs + AdsFade::kRaiseMs + 1;

// Raises the sights and lets the transition finish, returning the settled frame.
// The FIRST aiming frame only starts the fade - the pose is still whole there,
// which is the whole point of it being a fade rather than a switch - so a test
// about what a mode does during an aim has to run the transition out.
AdsPipeline::Pose AimSettled(AdsPipeline& ads, AdsMode mode,
                             const AdsPipeline::Pose& pose, unsigned long long startMs) {
    ads.Apply(mode, true, true, pose, startMs);
    return ads.Apply(mode, true, true, pose, startMs + AdsFade::kLowerMs);
}

// ---------------------------------------------------------------------------
// The entry pose: what the tracked modes measure from.

void EntryPoseTests() {
    std::printf("entry pose\n");

    {
        // Hip fire is the absolute pose, untouched, in every mode.
        AdsPipeline ads;
        const AdsPipeline::Pose out =
            ads.Apply(AdsMode::Tracked, false, true, Pose(30.0f, 10.0f, 5.0f, 0.1f, 0.2f, 0.3f), 0);
        Near(out.yaw, 30.0f, 1e-4f, "hip fire passes yaw through");
        Near(out.pitch, 10.0f, 1e-4f, "hip fire passes pitch through");
        Near(out.roll, 5.0f, 1e-4f, "hip fire passes roll through");
        Near(out.x, 0.1f, 1e-4f, "hip fire passes x through");
        Near(out.z, 0.3f, 1e-4f, "hip fire passes z through");
    }

    {
        // The entry frame is identity: aiming with the head held at 30 degrees
        // puts the view where the reticle was, not 30 degrees off it.
        AdsPipeline ads;
        ads.Apply(AdsMode::Tracked, false, true, Pose(30.0f, 10.0f, 5.0f), 0);
        const AdsPipeline::Pose entry =
            AimSettled(ads, AdsMode::Tracked, Pose(30.0f, 10.0f, 5.0f), 1);
        Near(entry.yaw, 0.0f, 1e-4f, "yaw is zero once the aim has settled on the entry pose");
        Near(entry.pitch, 0.0f, 1e-4f, "pitch is zero once the aim has settled on the entry pose");

        // Roll is NOT zeroed. Zeroing it levels a tilt the player is holding and
        // leans it back in as the weapon drops: two horizon jolts per aim.
        Near(entry.roll, 5.0f, 1e-4f, "roll stays absolute through the aim");

        // And head movement from there moves the view again.
        const AdsPipeline::Pose moved = ads.Apply(AdsMode::Tracked, true, true,
                                                  Pose(40.0f, 14.0f, 5.0f), kSettled);
        Near(moved.yaw, 10.0f, 1e-4f, "yaw tracks from the entry frame");
        Near(moved.pitch, 4.0f, 1e-4f, "pitch tracks from the entry frame");
    }

    {
        // Across the -180/180 seam the delta is the short way round. A plain
        // subtraction reads this 20 degree move as -340 and whips the view a
        // full turn the wrong way.
        AdsPipeline ads;
        ads.Apply(AdsMode::Tracked, true, true, Pose(170.0f, 0.0f, 0.0f), 0);
        const AdsPipeline::Pose out =
            ads.Apply(AdsMode::Tracked, true, true, Pose(-170.0f, 0.0f, 0.0f), kSettled);
        Near(out.yaw, 20.0f, 1e-3f, "yaw crosses the seam the short way");
    }

    {
        // Position goes relative too, so the eye starts on the barrel.
        AdsPipeline ads;
        ads.Apply(AdsMode::Marker, true, true, Pose(0.0f, 0.0f, 0.0f, 0.10f, 0.05f, -0.20f), 0);
        const AdsPipeline::Pose out = ads.Apply(
            AdsMode::Marker, true, true, Pose(0.0f, 0.0f, 0.0f, 0.15f, 0.05f, -0.10f), kSettled);
        Near(out.x, 0.05f, 1e-4f, "x is measured from the entry lean");
        Near(out.y, 0.0f, 1e-4f, "y is measured from the entry lean");
        Near(out.z, 0.10f, 1e-4f, "z is measured from the entry lean");
    }

    {
        // The capture waits for a live rotation. A frame whose interpolator has
        // nothing to give must not become the neutral the whole aim is measured
        // from - the path there is: aim, open a menu, move your head, come back
        // with the sights still up.
        AdsPipeline ads;
        ads.Apply(AdsMode::Tracked, true, false, Pose(50.0f, 0.0f, 0.0f), 0);
        ads.Apply(AdsMode::Tracked, true, true, Pose(20.0f, 0.0f, 0.0f), 1);
        const AdsPipeline::Pose out = ads.Apply(AdsMode::Tracked, true, true,
                                                Pose(20.0f, 0.0f, 0.0f), AdsFade::kLowerMs + 1);
        Near(out.yaw, 0.0f, 1e-4f, "the entry pose is captured from the first LIVE frame");
    }

    {
        // Lowering the weapon drops the entry pose, so the next aim measures
        // from where the head is then rather than from the old one.
        AdsPipeline ads;
        ads.Apply(AdsMode::Tracked, true, true, Pose(30.0f, 0.0f, 0.0f), 0);
        ads.Apply(AdsMode::Tracked, false, true, Pose(30.0f, 0.0f, 0.0f), kSettled);
        const AdsPipeline::Pose out =
            AimSettled(ads, AdsMode::Tracked, Pose(45.0f, 0.0f, 0.0f), kSettled * 2);
        Near(out.yaw, 0.0f, 1e-4f, "the entry pose is dropped when the weapon comes down");
    }

    {
        // Suppress() does the same, for every reason tracking stands down.
        AdsPipeline ads;
        ads.Apply(AdsMode::Tracked, true, true, Pose(30.0f, 0.0f, 0.0f), 0);
        ads.Suppress();
        const AdsPipeline::Pose out =
            AimSettled(ads, AdsMode::Tracked, Pose(45.0f, 0.0f, 0.0f), kSettled);
        Near(out.yaw, 0.0f, 1e-4f, "a suppressed frame drops the entry pose");
    }
}

// ---------------------------------------------------------------------------
// The gate: what each mode does with the pose while the sights are up.

void GateTests() {
    std::printf("mode gate\n");

    {
        // Paused takes the aim axes away entirely once the transition is done,
        // so the sight picture is the game's own.
        AdsPipeline ads;
        ads.Apply(AdsMode::Paused, false, true, Pose(30.0f, 10.0f, 5.0f, 0.1f, 0.1f, 0.1f), 0);
        const AdsPipeline::Pose out = AimSettled(
            ads, AdsMode::Paused, Pose(30.0f, 10.0f, 5.0f, 0.1f, 0.1f, 0.1f), 1);
        Near(out.yaw, 0.0f, 1e-4f, "paused removes yaw");
        Near(out.pitch, 0.0f, 1e-4f, "paused removes pitch");
        Near(out.x, 0.0f, 1e-4f, "paused removes the lean");
        Near(out.z, 0.0f, 1e-4f, "paused removes the forward lean");

        // Roll survives even here. It moves no aim point, and taking it away
        // would level a tilt the player is holding.
        Near(out.roll, 5.0f, 1e-4f, "paused leaves roll alone");
    }

    {
        // Marker and Tracked keep the pose live, measured from the entry frame.
        for (const AdsMode mode : {AdsMode::Marker, AdsMode::Tracked}) {
            AdsPipeline ads;
            ads.Apply(mode, true, true, Pose(30.0f, 10.0f, 0.0f), 0);
            const AdsPipeline::Pose out =
                ads.Apply(mode, true, true, Pose(45.0f, 10.0f, 0.0f), kSettled);
            Near(out.yaw, 15.0f, 1e-3f, "a tracked mode keeps tracking through the aim");
        }
    }

    {
        // The entry is a fade, not a switch: the frame the sights come up on is
        // still the full pose, and it is gone 150ms later.
        AdsPipeline ads;
        ads.Apply(AdsMode::Paused, false, true, Pose(20.0f, 0.0f, 0.0f), 0);
        const AdsPipeline::Pose first =
            ads.Apply(AdsMode::Paused, true, true, Pose(20.0f, 0.0f, 0.0f), 0);
        Near(first.yaw, 20.0f, 1e-3f, "the first aiming frame has not moved yet");

        const AdsPipeline::Pose mid =
            ads.Apply(AdsMode::Paused, true, true, Pose(20.0f, 0.0f, 0.0f), AdsFade::kLowerMs / 2);
        Check(mid.yaw > 0.5f && mid.yaw < 19.5f, "the transition is partway through at half time");

        const AdsPipeline::Pose done = ads.Apply(AdsMode::Paused, true, true,
                                                 Pose(20.0f, 0.0f, 0.0f), AdsFade::kLowerMs);
        Near(done.yaw, 0.0f, 1e-3f, "the transition is finished by kLowerMs");
    }

    {
        // A tap of the aim button reverses from where the transition IS. Ending
        // each leg at its own endpoint would remove a fully-applied pose in one
        // frame, which is the jolt the fade exists to prevent.
        AdsPipeline ads;
        ads.Apply(AdsMode::Paused, false, true, Pose(20.0f, 0.0f, 0.0f), 0);
        ads.Apply(AdsMode::Paused, true, true, Pose(20.0f, 0.0f, 0.0f), 0);
        const AdsPipeline::Pose pressed =
            ads.Apply(AdsMode::Paused, true, true, Pose(20.0f, 0.0f, 0.0f), 16);
        const AdsPipeline::Pose released =
            ads.Apply(AdsMode::Paused, false, true, Pose(20.0f, 0.0f, 0.0f), 32);
        Check(std::fabs(released.yaw - pressed.yaw) < 5.0f,
              "releasing a tap does not step the pose");
    }

    {
        // Changing mode mid-aim takes effect on that aim. The walk reads the
        // mode fresh every frame, so nothing has to be re-armed.
        AdsPipeline ads;
        ads.Apply(AdsMode::Paused, false, true, Pose(30.0f, 0.0f, 0.0f), 0);
        ads.Apply(AdsMode::Paused, true, true, Pose(30.0f, 0.0f, 0.0f), 1);
        const AdsPipeline::Pose paused =
            ads.Apply(AdsMode::Paused, true, true, Pose(45.0f, 0.0f, 0.0f), kSettled);
        Near(paused.yaw, 0.0f, 1e-3f, "paused mid-aim holds the pose off");

        const AdsPipeline::Pose tracked =
            ads.Apply(AdsMode::Tracked, true, true, Pose(45.0f, 0.0f, 0.0f), kSettled + 1);
        Near(tracked.yaw, 15.0f, 1e-3f, "switching to a tracked mode takes effect on this aim");
    }
}

}  // namespace

int main() {
    std::printf("ADS tests\n");
    EntryPoseTests();
    GateTests();
    if (g_failures == 0) {
        std::printf("all passed\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}

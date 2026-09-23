#include "ads_pose.h"

#include <cmath>
#include <cstdio>

using namespace DeusExHumanRevolutionHeadTracking;
using cameraunlock::ads::AdsFade;

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

AdsLean::Pose Pose(float yaw, float pitch, float roll, float x, float y, float z) {
    AdsLean::Pose p;
    p.yaw = yaw;
    p.pitch = pitch;
    p.roll = roll;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

const AdsLean::Pose kHead = Pose(30.0f, -12.0f, 8.0f, 0.10f, -0.05f, -0.20f);

void RotationUntouched(const AdsLean::Pose& out, const char* when) {
    char what[160];
    std::snprintf(what, sizeof(what), "yaw is absolute and unscaled %s", when);
    Near(out.yaw, kHead.yaw, 1e-5f, what);
    std::snprintf(what, sizeof(what), "pitch is absolute and unscaled %s", when);
    Near(out.pitch, kHead.pitch, 1e-5f, what);
    std::snprintf(what, sizeof(what), "roll is absolute and unscaled %s", when);
    Near(out.roll, kHead.roll, 1e-5f, what);
}

void HipFirePassesThrough() {
    std::printf("hip fire\n");
    AdsLean lean;
    for (unsigned long long t = 0; t < 1000; t += 16) {
        const AdsLean::Pose out = lean.Apply(false, kHead, t);
        RotationUntouched(out, "at the hip");
        Near(out.x, kHead.x, 1e-6f, "x passes through at the hip");
        Near(out.y, kHead.y, 1e-6f, "y passes through at the hip");
        Near(out.z, kHead.z, 1e-6f, "z passes through at the hip");
    }
}

void SightsUpDropsTheLeanOnly() {
    std::printf("sights up\n");
    AdsLean lean;
    lean.Apply(false, kHead, 0);

    // Raising the sights does not move the view: the first aiming frame is the
    // whole pose, rotation and lean alike.
    const AdsLean::Pose first = lean.Apply(true, kHead, 1);
    RotationUntouched(first, "on the frame the sights come up");
    Near(first.x, kHead.x, 1e-4f, "the lean has not started easing on the first frame");

    const AdsLean::Pose settled = lean.Apply(true, kHead, 1 + AdsFade::kLowerMs);
    RotationUntouched(settled, "with the sights up");
    Near(settled.x, 0.0f, 1e-6f, "x is zero with the sights up");
    Near(settled.y, 0.0f, 1e-6f, "y is zero with the sights up");
    Near(settled.z, 0.0f, 1e-6f, "z is zero with the sights up");

    // Head keeps moving through the aim, and rotation keeps following it.
    const AdsLean::Pose turned =
        lean.Apply(true, Pose(-25.0f, 5.0f, -10.0f, 0.2f, 0.1f, 0.1f), 2 + AdsFade::kLowerMs);
    Near(turned.yaw, -25.0f, 1e-5f, "rotation tracks the head through the aim");
    Near(turned.roll, -10.0f, 1e-5f, "roll tracks the head through the aim");
    Near(turned.x, 0.0f, 1e-6f, "the lean stays out while aiming");

    // And back at the hip the lean returns in full.
    lean.Apply(false, kHead, 3 + AdsFade::kLowerMs);
    const AdsLean::Pose back =
        lean.Apply(false, kHead, 3 + AdsFade::kLowerMs + AdsFade::kRaiseMs);
    RotationUntouched(back, "after the sights come down");
    Near(back.x, kHead.x, 1e-6f, "the lean is back in full once the weapon is down");
    Near(back.z, kHead.z, 1e-6f, "z is back in full once the weapon is down");
}

void MidTransitionScalesTheLean() {
    std::printf("mid-transition\n");
    AdsLean lean;
    lean.Apply(false, kHead, 0);
    lean.Apply(true, kHead, 0);
    const AdsLean::Pose mid = lean.Apply(true, kHead, AdsFade::kLowerMs / 2);
    RotationUntouched(mid, "mid-transition");

    const float scale = mid.x / kHead.x;
    Check(scale > 0.05f && scale < 0.95f, "the lean is partway out at half time");
    Near(mid.y, kHead.y * scale, 1e-5f, "y is scaled by the same fade as x");
    Near(mid.z, kHead.z * scale, 1e-5f, "z is scaled by the same fade as x");
}

void ReversalDoesNotStep() {
    std::printf("reversal\n");
    // A tap of the aim button: pressed, then released a frame or two later. The
    // return leg starts from where the fade is, so the lean never jumps.
    AdsLean lean;
    lean.Apply(false, kHead, 0);
    lean.Apply(true, kHead, 0);
    const AdsLean::Pose pressed = lean.Apply(true, kHead, 32);
    const AdsLean::Pose released = lean.Apply(false, kHead, 33);
    Near(released.x, pressed.x, 0.01f, "releasing a tap continues from where the fade was");
    RotationUntouched(released, "across a reversal");

    // Repeated taps: no frame-to-frame step larger than a smooth fade allows.
    AdsLean taps;
    float prevX = taps.Apply(false, kHead, 0).x;
    float worst = 0.0f;
    for (unsigned long long t = 16; t < 2000; t += 16) {
        const bool aiming = ((t / 48) % 2) == 1;
        const float x = taps.Apply(aiming, kHead, t).x;
        const float step = std::fabs(x - prevX);
        if (step > worst) worst = step;
        prevX = x;
    }
    Check(worst < kHead.x * 0.25f, "tapping the aim button never steps the lean");
}

void SuppressReturnsToTheHip() {
    std::printf("suppress\n");
    AdsLean lean;
    lean.Apply(false, kHead, 0);
    lean.Apply(true, kHead, 0);
    lean.Apply(true, kHead, AdsFade::kLowerMs);
    lean.Suppress();
    const AdsLean::Pose out = lean.Apply(false, kHead, AdsFade::kLowerMs + 1);
    Near(out.x, kHead.x, 1e-6f, "after a suppression the next hip frame has the full lean");
}

}  // namespace

int main() {
    std::printf("ADS lean tests\n");
    HipFirePassesThrough();
    SightsUpDropsTheLeanOnly();
    MidTransitionScalesTheLean();
    ReversalDoesNotStep();
    SuppressReturnsToTheHip();
    if (g_failures == 0) {
        std::printf("all passed\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}

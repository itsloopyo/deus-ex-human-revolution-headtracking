#include "config_sanitize.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace DeusExHumanRevolutionHeadTracking;

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

// The shipped default of each smoothing key. They are not the same number, and
// that is the whole point of the fallback argument.
const float kLocalDefault  = 0.0f;
const float kRemoteDefault = 0.15f;

void SmoothingTests() {
    std::printf("SanitizeSmoothing\n");
    Check(SanitizeSmoothing(0.0f, kLocalDefault) == 0.0f, "0 passes through");
    Check(SanitizeSmoothing(0.5f, kLocalDefault) == 0.5f, "in-range passes through");
    Check(SanitizeSmoothing(1.0f, kLocalDefault) == 1.0f, "1 passes through");

    // A configured zero is a real setting - track me with no added latency -
    // and it has to reach the processor as written. Nothing here may raise it,
    // least of all on the remote key, whose fallback is 0.15.
    Check(SanitizeSmoothing(0.0f, kRemoteDefault) == 0.0f,
          "a configured 0 survives verbatim on the remote key, never floored to 0.15");

    // Out of range saturates. Not because the math breaks - the core clamps its
    // own interpolation speed to [0.1, 50], so a smoothing above 1 no longer
    // drives the per-frame factor negative - but so the value the mod acts on
    // and the value the INI advertises stay the same number.
    Check(SanitizeSmoothing(5.0f, kLocalDefault) == 1.0f, "above 1 clamps to 1");
    Check(SanitizeSmoothing(-2.0f, kRemoteDefault) == 0.0f,
          "below 0 clamps to the bound, not to the fallback");

    // NaN/Inf would poison the smoothed quaternion and the view matrix, so they
    // take the fallback - and it is the fallback of the key that was read. A
    // malformed RemoteSmoothing answered with the LOCAL default would hand a
    // phone on WiFi zero smoothing on raw network jitter, which is the one case
    // RemoteSmoothing exists to cover.
    Check(SanitizeSmoothing(kNan, kLocalDefault) == 0.0f,
          "a NaN LocalSmoothing falls back to the local default 0.0");
    Check(SanitizeSmoothing(kNan, kRemoteDefault) == 0.15f,
          "a NaN RemoteSmoothing falls back to the remote default 0.15, not to 0.0");
    Check(SanitizeSmoothing(kInf, kRemoteDefault) == 0.15f,
          "an Inf RemoteSmoothing falls back to the remote default 0.15");
    Check(SanitizeSmoothing(-kInf, kRemoteDefault) == 0.15f,
          "a -Inf RemoteSmoothing falls back to the remote default 0.15");
    Check(SanitizeSmoothing(kInf, kLocalDefault) == 0.0f,
          "an Inf LocalSmoothing falls back to the local default 0.0");
    Check(SanitizeSmoothing(-kInf, kLocalDefault) == 0.0f,
          "a -Inf LocalSmoothing falls back to the local default 0.0");
}

void SensitivityTests() {
    std::printf("SanitizeSensitivity\n");
    Check(SanitizeSensitivity(1.0f) == 1.0f, "default passes through");
    Check(SanitizeSensitivity(2.5f) == 2.5f, "boost preserved");
    Check(SanitizeSensitivity(-1.0f) == -1.0f, "negative (invert) preserved");
    Check(SanitizeSensitivity(0.0f) == 0.0f, "zero preserved");
    Check(SanitizeSensitivity(kNan) == 1.0f, "NaN -> 1");
    Check(SanitizeSensitivity(kInf) == 1.0f, "Inf -> 1");
}

void LeanSkinTests() {
    std::printf("SanitizeLeanSkin\n");
    Check(SanitizeLeanSkin(0.19f, 0.19f) == 0.19f, "the default passes through");
    Check(SanitizeLeanSkin(0.25f, 0.19f) == 0.25f, "an in-range choice passes through");
    Check(SanitizeLeanSkin(0.0f, 0.19f) == 0.02f, "zero standoff -> the floor");
    Check(SanitizeLeanSkin(-1.0f, 0.19f) == 0.02f, "negative -> the floor");
    Check(SanitizeLeanSkin(5.0f, 0.19f) == 0.50f, "beyond every lean limit -> the ceiling");
    Check(SanitizeLeanSkin(kNan, 0.19f) == 0.19f, "NaN -> the key's own default");
    Check(SanitizeLeanSkin(kInf, 0.19f) == 0.19f, "Inf -> the key's own default");
}

void DeadzoneTests() {
    std::printf("SanitizeDeadzone\n");
    Check(SanitizeDeadzone(0.0f) == 0.0f, "0 passes through");
    Check(SanitizeDeadzone(3.0f) == 3.0f, "positive passes through");
    Check(SanitizeDeadzone(-1.0f) == 0.0f, "negative -> 0");
    Check(SanitizeDeadzone(kNan) == 0.0f, "NaN -> 0");
    Check(SanitizeDeadzone(kInf) == 0.0f, "Inf -> 0");
}

}

int main() {
    std::printf("DXHR config sanitize tests\n================================\n");
    SmoothingTests();
    SensitivityTests();
    LeanSkinTests();
    DeadzoneTests();
    if (g_failures == 0) {
        std::printf("All tests passed!\n");
        return 0;
    }
    std::printf("%d test(s) FAILED\n", g_failures);
    return 1;
}

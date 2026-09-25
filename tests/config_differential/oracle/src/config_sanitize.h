#pragma once

#include <cmath>

namespace DeusExHumanRevolutionHeadTracking {

// Boundary validation for floats read from the user-editable INI. The core
// library already finite-checks rotation values arriving over UDP
// (OpenTrackPacket::FiniteFloat); the same guarantee must hold for config
// values, which feed into the identical smoothing/quaternion math. A NaN/Inf
// from a malformed INI (e.g. "LocalSmoothing=nan") otherwise poisons the smoothed
// quaternion and the injected view matrix.

inline float SanitizeFinite(float v, float fallback) {
    return std::isfinite(v) ? v : fallback;
}

inline float ClampRange(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// LocalSmoothing and RemoteSmoothing must each be finite and within [0,1].
// [0,1] is the whole meaningful domain: CalculateSmoothingFactor maps it onto a
// settle speed between 50 (frame interpolation only) and 0.1 (roughly a five
// second settle), and the core clamps that speed to [0.1, 50] itself, so a
// value outside the range no longer drives the per-frame factor negative. It
// just saturates at one end while the INI goes on advertising a setting the mod
// is not honouring, so the clamp stays: it keeps the stored value and the
// behaviour in agreement, and gives the caller something to log.
//
// This is validation, never a floor. Any value inside [0,1] reaches the
// processor untouched, 0.0 included. `fallback` is the shipped default of the
// key being read, 0.0 for LocalSmoothing and 0.15 for RemoteSmoothing, so a
// malformed RemoteSmoothing lands on the remote default instead of silently
// handing a phone-over-WiFi user the local "no smoothing at all".
inline float SanitizeSmoothing(float v, float fallback) {
    return ClampRange(SanitizeFinite(v, fallback), 0.0f, 1.0f);
}

// Sensitivity: only NaN/Inf are unsafe (they propagate into the view matrix).
// Magnitude is a legitimate tuning choice (boost / invert), so it is not clamped.
inline float SanitizeSensitivity(float v) {
    return SanitizeFinite(v, 1.0f);
}

// The lean collision standoff, in metres. Finite, and inside a range that both
// ends of are a real setting: 0.02 m is the smallest standoff that is still a
// standoff, and 0.50 m exceeds every position limit the mod will ask for, so a
// value above it clamps every lean to nothing.
inline float SanitizeLeanSkin(float v, float fallback) {
    return ClampRange(SanitizeFinite(v, fallback), 0.02f, 0.50f);
}

// Deadzone in degrees: finite and non-negative.
inline float SanitizeDeadzone(float v) {
    float f = SanitizeFinite(v, 0.0f);
    return f < 0.0f ? 0.0f : f;
}

}

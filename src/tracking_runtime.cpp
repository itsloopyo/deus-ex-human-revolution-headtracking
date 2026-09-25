#include "tracking_runtime.h"

#include "ads.h"
#include "logging.h"

#include <windows.h>

#include <chrono>
#include <cstdint>

namespace DeusExHumanRevolutionHeadTracking {

bool TrackingRuntime::Start(const Config& cfg) {
    m_cfg = cfg;

    // The session forwards both values to the rotation AND position processors,
    // and re-reads the receiver's connection locality inside every Update() to
    // pick the one that applies. Without IsRemoteConnection() on the receiver
    // that selection silently pins to local, so assert the trait.
    static_assert(decltype(m_session)::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection()");
    m_session.SetLocalSmoothing(m_cfg.local_smoothing);
    m_session.SetRemoteSmoothing(m_cfg.remote_smoothing);

    // The table never loads a pair that names no mode: it reads both as their
    // defaults instead.
    m_session.SetMode(
        cameraunlock::DecodeTrackingMode(m_cfg.rotation_enabled, m_cfg.position_enabled).value());

    m_receiver.SetLog([](const std::string& msg) {
        Log::Line("UDP: %s", msg.c_str());
    });

    const uint16_t port = static_cast<uint16_t>(m_cfg.udp_port);
    if (m_receiver.Start(port)) {
        Log::Line("UDP receiver listening on port %u", port);
    } else {
        Log::Line("WARN: UDP receiver did not bind immediately on port %u; background retry active", port);
    }

    m_enabled.store(m_cfg.enable_on_startup, std::memory_order_relaxed);
    m_worldSpaceYaw.store(m_cfg.world_space_yaw, std::memory_order_relaxed);
    return true;
}

void TrackingRuntime::Stop() {
    m_receiver.Stop();
}

void TrackingRuntime::ToggleEnabled() {
    bool prev = m_enabled.load(std::memory_order_relaxed);
    m_enabled.store(!prev, std::memory_order_relaxed);
    Log::Line("Tracking %s", !prev ? "enabled" : "disabled");
}

cameraunlock::TrackingMode TrackingRuntime::CycleTrackingMode() {
    const cameraunlock::TrackingMode mode = m_session.CycleMode();
    switch (mode) {
        case cameraunlock::TrackingMode::RotationAndPosition:
            Log::Line("Tracking mode: rotation + position (normal)");
            break;
        case cameraunlock::TrackingMode::RotationOnly:
            Log::Line("Tracking mode: rotation only (position disabled)");
            break;
        case cameraunlock::TrackingMode::PositionOnly:
            Log::Line("Tracking mode: position only (rotation disabled)");
            break;
    }
    return mode;
}

bool TrackingRuntime::ToggleYawMode() {
    bool prev = m_worldSpaceYaw.load(std::memory_order_relaxed);
    m_worldSpaceYaw.store(!prev, std::memory_order_relaxed);
    Log::Line("Yaw mode: %s", !prev ? "world-space (horizon-locked)" : "camera-local");
    return !prev;
}

bool TrackingRuntime::IsPoseFresh() const {
    const std::int64_t lastUs = m_receiver.GetLastReceiveTimestamp();
    if (lastUs == 0) {
        return false;
    }
    const std::int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return (nowUs - lastUs) / 1000 < m_cfg.data_freshness_ms;
}

bool TrackingRuntime::SamplePerFrame(HeadPose& out) {
    out = HeadPose{};

    m_lastDt = m_clock.Tick();

    if (!m_enabled.load(std::memory_order_relaxed) ||
        !IsPoseFresh() ||
        !m_session.Update(m_lastDt)) {
        m_adsLean.Suppress();
        return false;
    }

    AdsLean::Pose absolute{};
    const bool rotationValid =
        m_session.GetRotation(absolute.yaw, absolute.pitch, absolute.roll);
    const bool positionValid = m_session.IsPositionActive() &&
                               m_session.GetPositionOffset(absolute.x, absolute.y, absolute.z);
    if (!rotationValid && !positionValid) {
        m_adsLean.Suppress();
        return false;
    }

    // Asked last, after every gate above has had its say, so a frame tracking
    // stands down on never reads the sights at all.
    const AdsLean::Pose shaped = m_adsLean.Apply(SightsAreUp(), absolute, GetTickCount64());

    out.rotation_valid = rotationValid;
    out.yaw = shaped.yaw;
    out.pitch = shaped.pitch;
    out.roll = shaped.roll;
    out.position_valid = positionValid;
    out.x = shaped.x;
    out.y = shaped.y;
    out.z = shaped.z;
    return true;
}

}

#include "tracking_runtime.h"

#include "ads.h"
#include "logging.h"

#include <windows.h>

#include <chrono>
#include <cstdint>

namespace DeusExHumanRevolutionHeadTracking {

bool TrackingRuntime::Start(const Config& cfg, const std::string& iniPath) {
    m_cfg = cfg;
    m_iniPath = iniPath;

    cameraunlock::SensitivitySettings sens;
    sens.yaw = m_cfg.sens_yaw;
    sens.pitch = m_cfg.sens_pitch;
    sens.roll = m_cfg.sens_roll;
    sens.invert_yaw = m_cfg.invert_yaw;
    sens.invert_pitch = m_cfg.invert_pitch;
    sens.invert_roll = m_cfg.invert_roll;
    m_session.GetProcessor().SetSensitivity(sens);

    cameraunlock::DeadzoneSettings dz;
    dz.yaw = dz.pitch = dz.roll = m_cfg.deadzone_deg;
    m_session.GetProcessor().SetDeadzone(dz);

    // The session forwards both values to the rotation AND position processors,
    // and re-reads the receiver's connection locality inside every Update() to
    // pick the one that applies. Without IsRemoteConnection() on the receiver
    // that selection silently pins to local, so assert the trait.
    static_assert(decltype(m_session)::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection()");
    m_session.SetLocalSmoothing(m_cfg.local_smoothing);
    m_session.SetRemoteSmoothing(m_cfg.remote_smoothing);

    m_session.SetMode(m_cfg.position_enabled
                          ? cameraunlock::TrackingMode::RotationAndPosition
                          : cameraunlock::TrackingMode::RotationOnly);

    m_receiver.SetLog([](const std::string& msg) {
        Log::Line("UDP: %s", msg.c_str());
    });

    if (m_receiver.Start(m_cfg.udp_port)) {
        Log::Line("UDP receiver listening on port %u", m_cfg.udp_port);
    } else {
        Log::Line("WARN: UDP receiver did not bind immediately on port %u; background retry active", m_cfg.udp_port);
    }

    m_enabled.store(m_cfg.enabled_on_startup, std::memory_order_relaxed);
    m_worldSpaceYaw.store(m_cfg.world_space_yaw, std::memory_order_relaxed);
    m_adsMode.store(m_cfg.ads_mode, std::memory_order_relaxed);
    Log::Line("ADS mode: %s", cameraunlock::ads::AdsModeLabel(m_cfg.ads_mode));
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

void TrackingRuntime::CycleTrackingMode() {
    switch (m_session.CycleMode()) {
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
}

void TrackingRuntime::CycleAdsMode() {
    const cameraunlock::ads::AdsMode next =
        cameraunlock::ads::NextAdsMode(m_adsMode.load(std::memory_order_relaxed));
    m_adsMode.store(next, std::memory_order_relaxed);

    // The mod has no on-screen text of its own - the aim marker overlay draws
    // primitives, not glyphs - so the log line is the toast. It carries the
    // fleet's wording so a player reading it recognises the mode from any other
    // shooter.
    Log::Line("%s", cameraunlock::ads::AdsModeToast(next));

    if (!SaveAdsMode(m_iniPath.c_str(), next)) {
        Log::Line("WARN: could not write AdsMode back to %s. The mode applies now "
                  "but will not survive a restart; check the file is writable.",
                  m_iniPath.c_str());
    }
}

void TrackingRuntime::ToggleYawMode() {
    bool prev = m_worldSpaceYaw.load(std::memory_order_relaxed);
    m_worldSpaceYaw.store(!prev, std::memory_order_relaxed);
    Log::Line("Yaw mode: %s", !prev ? "world-space (horizon-locked)" : "camera-local");
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

    // Every one of these is a real suppression, so the ADS transition and the
    // entry pose are dropped rather than carried across the gap: coming back
    // with the sights still up has to re-enter against the head where it now is.
    if (!m_enabled.load(std::memory_order_relaxed) ||
        !IsPoseFresh() ||
        !m_session.Update(m_lastDt)) {
        m_ads.Suppress();
        return false;
    }

    AdsPipeline::Pose absolute{};
    const bool rotationValid =
        m_session.GetRotation(absolute.yaw, absolute.pitch, absolute.roll);
    const bool positionValid = m_session.IsPositionActive() &&
                               m_session.GetPositionOffset(absolute.x, absolute.y, absolute.z);
    if (!rotationValid && !positionValid) {
        m_ads.Suppress();
        return false;
    }

    // The sights come from the game's own state, polled on this frame. Never
    // from the verdict above: in AdsMode::Paused the fade is what takes the pose
    // away, so feeding our own answer back in would make it restart itself.
    const AdsPipeline::Pose shaped =
        m_ads.Apply(m_adsMode.load(std::memory_order_relaxed), SightsAreUp(),
                    rotationValid, absolute, GetTickCount64());

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

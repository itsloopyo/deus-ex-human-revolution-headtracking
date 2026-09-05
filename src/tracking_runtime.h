#pragma once

#include "ads_pose.h"
#include "config.h"

#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"

#include <atomic>
#include <string>

namespace DeusExHumanRevolutionHeadTracking {

struct HeadPose {
    bool  rotation_valid = false;
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;  // degrees
    bool  position_valid = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;           // meters
};

class TrackingRuntime {
public:
    TrackingRuntime() : m_session(m_receiver) {}

    bool Start(const Config& cfg, const std::string& iniPath);
    void Stop();

    // Called once per rendered frame from the camera hook. Advances the session
    // clock once and fills the latest processed rotation (degrees) and position
    // offset (meters). Returns false when tracking is disabled or no fresh data.
    bool SamplePerFrame(HeadPose& out);

    // The delta time SamplePerFrame last advanced the session by. The camera
    // hook's lean clamp damps its release over real time, so it needs the same
    // clock the pose came off rather than one of its own.
    float LastFrameDt() const { return m_lastDt; }

    void ToggleEnabled();
    void CycleTrackingMode();

    void ToggleYawMode();
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(std::memory_order_relaxed); }

    // Advances the ADS cycle and persists the choice. The per-frame walk reads
    // the mode fresh every frame, so a change made mid-aim takes effect on that
    // aim rather than on the next one.
    void CycleAdsMode();
    cameraunlock::ads::AdsMode GetAdsMode() const {
        return m_adsMode.load(std::memory_order_relaxed);
    }

private:
    // Frame dt is clamped to this ceiling so a stall (alt-tab, load hitch) cannot
    // feed a huge dt into the smoothing/extrapolation math.
    static constexpr float kMaxFrameDtSec = 0.25f;

    // True while the newest packet is younger than Config::data_freshness_ms. The
    // core receiver's own IsReceiving() is fixed at 500ms, which is where the
    // shipped default comes from; this is what makes a user-chosen window mean
    // anything.
    bool IsPoseFresh() const;

    Config m_cfg{};
    float m_lastDt = 0.0f;
    cameraunlock::UdpReceiver m_receiver;
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session;
    cameraunlock::time::FrameClock m_clock{kMaxFrameDtSec};

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_worldSpaceYaw{true};
    std::atomic<cameraunlock::ads::AdsMode> m_adsMode{cameraunlock::ads::kDefaultAdsMode};

    // Touched only from the camera hook's frame, which is the one thread that
    // calls SamplePerFrame.
    AdsPipeline m_ads;
    std::string m_iniPath;
};

}

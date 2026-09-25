#pragma once

// The oracle: the config reader of the newest published build (the rolling
// `dev` pre-release, 9a3d6ce), compiled from oracle/ with the core sources it
// included at its pin (bb4a0f6). Its namespaces are renamed at compile time so
// it links beside the current core. This header names no core type, so the test
// includes it without the renaming.

#include <string>

namespace dxhr_oracle_view {

struct OracleConfig {
    bool enabled_on_startup;
    int udp_port;
    float sens_yaw, sens_pitch, sens_roll;
    bool invert_yaw, invert_pitch, invert_roll;
    float local_smoothing, remote_smoothing, deadzone_deg;
    int data_freshness_ms;
    bool world_space_yaw;
    bool position_enabled;
    bool lean_collision;
    float lean_collision_skin_m;
    // cameraunlock::ads::AdsMode at bb4a0f6: 0 paused, 1 marker, 2 tracked.
    int ads_mode;
    bool camera_dump;
    bool reticle_probe;
    int vk_toggle, vk_position, vk_yaw_mode, vk_ads;
    bool chord_toggle, chord_position, chord_yaw_mode, chord_ads;
};

struct OracleResult {
    // What Config::LoadOrCreate returned. false stopped the mod at startup.
    bool loaded;
    OracleConfig config;
};

// Config::LoadOrCreate on a default Config, as the published build's
// InitThread ran it. Creates the file when there is none, as that build did.
OracleResult RunOracle(const std::string& path);

}

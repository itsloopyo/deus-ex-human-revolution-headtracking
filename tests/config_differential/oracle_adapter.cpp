// Compiled into the oracle library only, with `cameraunlock` and
// `DeusExHumanRevolutionHeadTracking` renamed, so "config.h" here is the
// published build's (oracle/src/config.h).
#include "config.h"
#include "oracle_adapter.h"

namespace dxhr_oracle_view {

OracleResult RunOracle(const std::string& path) {
    DeusExHumanRevolutionHeadTracking::Config c;
    const bool loaded = c.LoadOrCreate(path.c_str());
    OracleConfig o{};
    o.enabled_on_startup = c.enabled_on_startup;
    o.udp_port = c.udp_port;
    o.sens_yaw = c.sens_yaw;
    o.sens_pitch = c.sens_pitch;
    o.sens_roll = c.sens_roll;
    o.invert_yaw = c.invert_yaw;
    o.invert_pitch = c.invert_pitch;
    o.invert_roll = c.invert_roll;
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.deadzone_deg = c.deadzone_deg;
    o.data_freshness_ms = c.data_freshness_ms;
    o.world_space_yaw = c.world_space_yaw;
    o.position_enabled = c.position_enabled;
    o.lean_collision = c.lean_collision;
    o.lean_collision_skin_m = c.lean_collision_skin_m;
    o.ads_mode = static_cast<int>(c.ads_mode);
    o.camera_dump = c.camera_dump;
    o.reticle_probe = c.reticle_probe;
    o.vk_toggle = c.vk_toggle;
    o.vk_position = c.vk_position;
    o.vk_yaw_mode = c.vk_yaw_mode;
    o.vk_ads = c.vk_ads;
    o.chord_toggle = c.chord_toggle;
    o.chord_position = c.chord_position;
    o.chord_yaw_mode = c.chord_yaw_mode;
    o.chord_ads = c.chord_ads;
    return {loaded, o};
}

}

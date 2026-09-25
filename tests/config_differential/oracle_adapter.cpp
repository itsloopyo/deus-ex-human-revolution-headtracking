// Compiled into the oracle library only, with `cameraunlock` and
// `DeusExHumanRevolutionHeadTracking` renamed, so "config.h" here is the
// published build's (oracle/src/config.h), "hotkeys.h" is its Hotkeys and the
// poller under it is oracle_fake's.
#include "config.h"
#include "hotkeys.h"
#include "oracle_adapter.h"

#include <stdexcept>

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

FireTable OracleFires(const HotkeyView& keys) {
    namespace input = cameraunlock::input;
    DeusExHumanRevolutionHeadTracking::Config c;
    c.vk_toggle = keys.vk_toggle;
    c.vk_position = keys.vk_position;
    c.vk_yaw_mode = keys.vk_yaw_mode;
    c.vk_ads = 0;
    c.chord_toggle = keys.chord_toggle;
    c.chord_position = keys.chord_position;
    c.chord_yaw_mode = keys.chord_yaw_mode;
    c.chord_ads = false;

    std::array<int, 3> fired{};
    input::FakeRegistrations().clear();
    DeusExHumanRevolutionHeadTracking::Hotkeys hotkeys;
    if (!hotkeys.Start(c, [&fired] { ++fired[0]; }, [&fired] { ++fired[1]; }, [&fired] { ++fired[2]; }, [] {})) {
        throw std::logic_error("the published Hotkeys::Start failed on the fake poller");
    }
    const std::vector<input::FakeRegistration> registered = input::FakeRegistrations();
    hotkeys.Stop();

    // The published poller's Poll: a callback runs when its nonzero key goes down.
    FireTable table;
    table.reserve((kLastKey - kFirstKey + 1) * kHeldStates);
    for (int vk = kFirstKey; vk <= kLastKey; ++vk) {
        for (int held = 0; held < kHeldStates; ++held) {
            fired = {};
            input::FakeHeld() = held;
            for (const input::FakeRegistration& r : registered) {
                if (r.vk == vk && r.callback) r.callback();
            }
            table.push_back(fired);
        }
    }
    input::FakeHeld() = 0;
    return table;
}

}

#include "config.h"

#include "legacy_config/legacy_config.h"

#include "cameraunlock/config/head_tracking_config_table.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <string>
#include <utility>
#include <vector>

namespace DeusExHumanRevolutionHeadTracking {

namespace {

using cameraunlock::config::DropRule;
using cameraunlock::config::DroppedValue;
using cameraunlock::config::ImportResult;
using cameraunlock::config::LegacyInput;
using cameraunlock::config::LegacyPoseShaping;
using cameraunlock::config::PoseShapingValue;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;

// A legacy hotkey code and its Ctrl+Shift chord switch as one key list: the
// code's binding when it is a key code, then the chord.
std::string KeyList(int vk, bool chord, char letter, const char* key, std::vector<DroppedValue>& dropped) {
    cameraunlock::config::LegacyVirtualKeyToBindings(vk, "Hotkeys", key, dropped);
    std::vector<KeyBinding> bindings;
    if (vk >= 0x01 && vk <= 0xFE) bindings.push_back({KeyModifiers::kNone, vk});
    if (chord) bindings.push_back({KeyModifiers::kCtrl | KeyModifiers::kShift, letter});
    return cameraunlock::input::FormatKeyBindings(bindings);
}

ImportResult Import(const LegacyInput& input, Config& out) {
    legacy::Config c;
    const legacy::ReadResult read = c.Read(input.ansi_path.c_str());
    if (read.status == legacy::ReadStatus::Refused) {
        return ImportResult::Refused(read.reason);
    }

    std::vector<DroppedValue> dropped;
    std::vector<PoseShapingValue> shaping;

    out.enable_on_startup = c.enabled_on_startup;
    out.udp_port = c.udp_port;
    out.data_freshness_ms = c.data_freshness_ms;
    out.world_space_yaw = c.world_space_yaw;

    // [General] PositionEnabled chose only the startup mode: the cycle key
    // reached every mode either way.
    const cameraunlock::TrackingModeChannels mode = cameraunlock::EncodeTrackingMode(
        c.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                           : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = mode.rotation_enabled;
    out.position_enabled = mode.position_enabled;

    out.local_smoothing = c.local_smoothing;
    out.position.local_smoothing = c.local_smoothing;
    out.remote_smoothing = c.remote_smoothing;
    out.position.remote_smoothing = c.remote_smoothing;

    out.collision_enabled = c.lean_collision;
    out.lean_clamp.skin = c.lean_collision_skin_m;
    out.camera_dump = c.camera_dump;

    // Every one the build shipped was identity, so nothing folds into the
    // axis code and a value the player changed is dropped.
    LegacyPoseShaping(c.sens_yaw, 1.0f, "Sensitivity", "Yaw", shaping, dropped);
    LegacyPoseShaping(c.sens_pitch, 1.0f, "Sensitivity", "Pitch", shaping, dropped);
    LegacyPoseShaping(c.sens_roll, 1.0f, "Sensitivity", "Roll", shaping, dropped);
    LegacyPoseShaping(c.invert_yaw, false, "Sensitivity", "InvertYaw", shaping, dropped);
    LegacyPoseShaping(c.invert_pitch, false, "Sensitivity", "InvertPitch", shaping, dropped);
    LegacyPoseShaping(c.invert_roll, false, "Sensitivity", "InvertRoll", shaping, dropped);
    LegacyPoseShaping(c.deadzone_deg, 0.0f, "Smoothing", "DeadzoneDeg", shaping, dropped);

    // The probe swept the game's reticle instead of placing it on the aim.
    if (c.reticle_probe) dropped.push_back({DropRule::Reticle, "General", "ReticleProbe", "true"});

    out.toggle_key_name = KeyList(c.vk_toggle, c.chord_toggle, 'Y', "Toggle", dropped);
    out.cycle_tracking_mode_key_name = KeyList(c.vk_position, c.chord_position, 'G', "Position", dropped);
    out.yaw_mode_key_name = KeyList(c.vk_yaw_mode, c.chord_yaw_mode, 'H', "YawMode", dropped);

    return read.status == legacy::ReadStatus::Absent ? ImportResult::Absent(std::move(dropped), std::move(shaping))
                                                     : ImportResult::Imported(std::move(dropped), std::move(shaping));
}

}

cameraunlock::config::ConfigTable<Config> MakeConfigTable() {
    using cameraunlock::config::schema::Concept;
    cameraunlock::config::ConfigTable<Config> table = cameraunlock::config::HeadTrackingConfigTable<Config>(
        {Concept::UdpPort, Concept::EnableOnStartup, Concept::WorldSpaceYaw, Concept::RotationEnabled,
         Concept::DataFreshnessMs, Concept::LocalSmoothing, Concept::RemoteSmoothing, Concept::PositionEnabled,
         Concept::CollisionEnabled, Concept::CollisionMargin, Concept::ToggleKey, Concept::CycleTrackingModeKey,
         Concept::YawModeKey});
    table.Select(Concept::WorldSpaceYaw).Writable()
        .Select(Concept::RotationEnabled).Writable()
        .Select(Concept::PositionEnabled).Writable();
    table.Select(Concept::CollisionMargin)
        .Comment("How far, in metres, the view is held off a wall when you lean into it.\n"
                 "A value inside the camera's near clip plane is raised to clear it.");
    table.Local("Diagnostics", "CameraDump", &Config::camera_dump, cameraunlock::config::BoolCodec(),
                "true: write camera and reticle diagnostics to HeadTracking.log, for troubleshooting.");
    return table;
}

cameraunlock::config::LegacyImport<Config> MakeLegacyImport() {
    return {&Import, legacy::ReadKeys()};
}

cameraunlock::config::ConfigOwnerOptions<Config> MakeOwnerOptions(const std::filesystem::path& folder,
                                                                  cameraunlock::config::DefaultsFile defaults) {
    cameraunlock::config::ConfigOwnerOptions<Config> options;
    options.path = (folder / kConfigFileName).wstring();
    options.table = MakeConfigTable();
    options.import = MakeLegacyImport();
    options.legacy_path = (folder / kLegacyFileName).wstring();
    options.header.display_name = kConfigDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

}

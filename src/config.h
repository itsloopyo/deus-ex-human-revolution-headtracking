#pragma once

#include "cameraunlock/config/config_table.h"
#include "cameraunlock/config/head_tracking_config.h"
#include "cameraunlock/config/legacy_import.h"

namespace DeusExHumanRevolutionHeadTracking {

constexpr const char* kConfigFileName = "DeusExHumanRevolutionHeadTracking.ini";
// The game's name as cameraunlock-core's data/games.json spells it.
constexpr const char* kConfigDisplayName = "Deus Ex: Human Revolution - Director's Cut";

// Core's config with this game's defaults. The lean collision margin
// (lean_clamp.skin) is in metres here, not in the engine's units: the camera
// hook converts it, and raises it to clear the near clip plane.
struct Config : cameraunlock::HeadTrackingConfig {
    // Discovery aid: the camera and reticle diagnostic lines in the log.
    bool camera_dump = false;

    Config() {
        collision_enabled = true;
        lean_clamp.skin = 0.19f;
    }
};

cameraunlock::config::ConfigTable<Config> MakeConfigTable();

// The pre-canonical reader (legacy_config/), mapped into Config.
cameraunlock::config::LegacyImport<Config> MakeLegacyImport();

}

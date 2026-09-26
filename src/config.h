#pragma once

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/config_table.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/head_tracking_config.h"
#include "cameraunlock/config/legacy_import.h"

#include <filesystem>

namespace DeusExHumanRevolutionHeadTracking {

constexpr const char* kConfigFileName = "CameraUnlock.ini";
// The file every build before the canonical format read, beside CameraUnlock.ini.
// It is imported once while CameraUnlock.ini is absent and never written, so an
// older build still reads it after a rollback.
constexpr const char* kLegacyFileName = "DeusExHumanRevolutionHeadTracking.ini";
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

// The owner of CameraUnlock.ini in `folder`, importing the legacy file beside it.
// The mod passes the player's own Defaults.ini, and every test a scratch one.
cameraunlock::config::ConfigOwnerOptions<Config> MakeOwnerOptions(const std::filesystem::path& folder,
                                                                  cameraunlock::config::DefaultsFile defaults);

}

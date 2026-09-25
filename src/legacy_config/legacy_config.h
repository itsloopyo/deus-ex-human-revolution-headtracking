#pragma once

// The config reader of the last build that read DeusExHumanRevolutionHeadTracking.ini
// in its pre-canonical layout, frozen so a player updating from any older build
// is converted exactly as that build read the file. Nothing in this folder is
// ever edited. Three things differ from the reader it was taken from: it fills
// this frozen copy of that build's Config and defaults rather than the runtime
// type, it never writes the file (a missing file reads as the defaults), and it
// reports a refusal apart from an absent file.

#include <cstdint>
#include <string>
#include <vector>

#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/math/smoothing_utils.h"

namespace DeusExHumanRevolutionHeadTracking::legacy {

enum class ReadStatus {
    Read,
    // No file at the path. Config holds the defaults, which is what the old
    // reader read from the file it created there.
    Absent,
    // The old reader returned false and the mod did not start.
    Refused,
};

struct ReadResult {
    ReadStatus status = ReadStatus::Read;
    // For Refused, the line the old reader logged.
    std::string reason;
};

struct Config {
    bool  enabled_on_startup = true;
    uint16_t udp_port = 4242;

    float sens_yaw = 1.0f;
    float sens_pitch = 1.0f;
    float sens_roll = 1.0f;
    bool  invert_yaw = false;
    bool  invert_pitch = false;
    bool  invert_roll = false;

    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);
    float deadzone_deg = 0.0f;

    int  data_freshness_ms = 500;

    bool world_space_yaw = true;

    bool position_enabled = true;

    bool lean_collision = true;

    float lean_collision_skin_m = 0.19f;

    bool camera_dump = false;

    bool reticle_probe = false;

    int vk_toggle    = 0x23; // VK_END
    int vk_position  = 0x21; // VK_PRIOR (Page Up) - cycle tracking mode
    int vk_yaw_mode  = 0x22; // VK_NEXT (Page Down)
    bool chord_toggle = true;
    bool chord_position = true;
    bool chord_yaw_mode = true;

    // Call on a default-constructed Config.
    ReadResult Read(const char* iniPath);
};

// Every section and key Read reads, in the order it reads them.
std::vector<cameraunlock::config::LegacyKey> ReadKeys();

}

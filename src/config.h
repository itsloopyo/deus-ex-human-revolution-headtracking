#pragma once

#include <cstdint>

#include "cameraunlock/math/smoothing_utils.h"

namespace DeusExHumanRevolutionHeadTracking {

struct Config {
    bool  enabled_on_startup = true;
    uint16_t udp_port = 4242;

    float sens_yaw = 1.0f;
    float sens_pitch = 1.0f;
    float sens_roll = 1.0f;
    bool  invert_yaw = false;
    bool  invert_pitch = false;
    bool  invert_roll = false;

    // Smoothing is picked per connection from the packet source address:
    // loopback senders get local_smoothing, remote network devices get
    // remote_smoothing. Both cover rotation and position.
    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);
    float deadzone_deg = 0.0f;

    int  data_freshness_ms = 500;

    // true = horizon-locked (world-space) yaw, false = camera-local yaw.
    bool world_space_yaw = true;

    // 6DOF position tracking on/off.
    bool position_enabled = true;

    // Cut the lean down to whatever the level leaves room for, by asking the
    // game's own collision world what is between the eye and where the tracker
    // wants it. Off means the eye goes where the tracker asks and leaning into a
    // wall puts the view through it.
    bool lean_collision = true;

    // How far off a blocking surface to hold the eye, in metres. A value inside
    // the camera's near clip plane is raised to clear it: geometry nearer than
    // that plane is never drawn, so the wall the eye stopped short of opens into
    // a cutaway and the clamp shows the artefact it exists to prevent. The
    // default already clears the near plane this game floors itself at.
    float lean_collision_skin_m = 0.19f;

    // Discovery aid: dump the baked camera matrix to the log instead of
    // injecting head rotation. Used while pinning the matrix layout.
    bool camera_dump = false;

    // Discovery aid: sweep the reticle across the screen instead of correcting
    // it, to establish whether the movie honours the property we move it with.
    bool reticle_probe = false;

    int vk_toggle    = 0x23; // VK_END
    int vk_position  = 0x21; // VK_PRIOR (Page Up) - cycle tracking mode
    int vk_yaw_mode  = 0x22; // VK_NEXT (Page Down)
    bool chord_toggle = true;
    bool chord_position = true;
    bool chord_yaw_mode = true;

    bool LoadOrCreate(const char* iniPath);
};

}

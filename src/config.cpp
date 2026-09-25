#include "config.h"

#include "legacy_config/legacy_config.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"

#include <cstdio>
#include <fstream>

namespace DeusExHumanRevolutionHeadTracking {

namespace {

// The defaults WriteDefaultIni writes on first run. The frozen reader in
// legacy_config/ holds its own copy, as the build it was taken from did.
constexpr bool  kDefaultEnableOnStartup = true;
constexpr int   kDefaultPort            = 4242;
constexpr int   kDefaultDataFreshnessMs = 500;
constexpr bool  kDefaultWorldSpaceYaw   = true;
constexpr float kDefaultSensitivity     = 1.0f;
constexpr bool  kDefaultInvert          = false;
constexpr float kDefaultLocalSmoothing  =
    static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
constexpr float kDefaultRemoteSmoothing =
    static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);
constexpr float kDefaultDeadzoneDeg     = 0.0f;
constexpr bool  kDefaultPositionEnabled = true;
constexpr bool  kDefaultLeanCollision   = true;
constexpr float kDefaultLeanSkinM       = 0.19f;
constexpr bool  kDefaultCameraDump      = false;
constexpr bool  kDefaultReticleProbe    = false;
constexpr int   kDefaultVkToggle        = 0x23; // VK_END
constexpr int   kDefaultVkPosition      = 0x21; // VK_PRIOR (Page Up)
constexpr int   kDefaultVkYawMode       = 0x22; // VK_NEXT (Page Down)
constexpr bool  kDefaultChord           = true;

bool FileExists(const char* path) {
    std::ifstream f(path);
    return f.good();
}

void WriteDefaultIni(const char* path) {
    cameraunlock::IniWriter w;
    if (!w.Open(path)) return;
    w.WriteComment(" Deus Ex: Human Revolution - Director's Cut - Head Tracking configuration");
    w.WriteComment(" Lives next to DXHRDC.exe in the game install root.");
    w.WriteBlankLine();
    w.WriteSection("General");
    w.WriteBool("EnableOnStartup", kDefaultEnableOnStartup);
    w.WriteInt("Port", kDefaultPort);
    w.WriteInt("DataFreshnessMs", kDefaultDataFreshnessMs);
    w.WriteComment(" Yaw mode: true = horizon-locked yaw (default), false = camera-local.");
    w.WriteBool("WorldSpaceYaw", kDefaultWorldSpaceYaw);
    w.WriteBool("PositionEnabled", kDefaultPositionEnabled);
    w.WriteComment(" Stop the lean where the level does: the eye is held off walls by asking");
    w.WriteComment(" the game's own collision, instead of passing through them.");
    w.WriteBool("LeanCollision", kDefaultLeanCollision);
    w.WriteComment(" How far off a surface to hold the eye, in metres. Anything below the");
    w.WriteComment(" camera's near clip plane is raised to clear it - closer than that the");
    w.WriteComment(" wall is not drawn at all and you see through it.");
    w.WriteDouble("LeanCollisionSkin", kDefaultLeanSkinM);
    w.WriteComment(" Discovery aid only: dump the camera matrix to the log instead of tracking.");
    w.WriteBool("CameraDump", kDefaultCameraDump);
    w.WriteComment(" Discovery aid only: sweep the reticle across the screen instead of");
    w.WriteComment(" correcting it, to check the game moves it where this mod asks.");
    w.WriteBool("ReticleProbe", kDefaultReticleProbe);
    w.WriteBlankLine();
    w.WriteSection("Sensitivity");
    w.WriteDouble("Yaw", kDefaultSensitivity);
    w.WriteDouble("Pitch", kDefaultSensitivity);
    w.WriteDouble("Roll", kDefaultSensitivity);
    w.WriteBool("InvertYaw", kDefaultInvert);
    w.WriteBool("InvertPitch", kDefaultInvert);
    w.WriteBool("InvertRoll", kDefaultInvert);
    w.WriteBlankLine();
    w.WriteSection("Smoothing");
    w.WriteComment(" Smoothing applied when the tracker runs on this machine (loopback).");
    w.WriteComment(" 0 = no smoothing, 1 = heavy. Covers rotation and position.");
    w.WriteDouble("LocalSmoothing", kDefaultLocalSmoothing);
    w.WriteComment(" Smoothing applied when the tracker is a remote device on the network.");
    w.WriteComment(" 0 = no smoothing, 1 = heavy. Covers rotation and position.");
    w.WriteDouble("RemoteSmoothing", kDefaultRemoteSmoothing);
    w.WriteDouble("DeadzoneDeg", kDefaultDeadzoneDeg);
    w.WriteBlankLine();
    w.WriteSection("Hotkeys");
    w.WriteComment(" Virtual-key codes. Defaults: End (toggle), Page Up (cycle tracking mode), Page Down (yaw mode).");
    w.WriteHex("Toggle", kDefaultVkToggle);
    w.WriteHex("Position", kDefaultVkPosition);
    w.WriteHex("YawMode", kDefaultVkYawMode);
    w.WriteComment(" Chord alternatives: Ctrl+Shift+Y (toggle), Ctrl+Shift+G (cycle tracking mode), Ctrl+Shift+H (yaw mode).");
    w.WriteBool("ChordToggle", kDefaultChord);
    w.WriteBool("ChordPosition", kDefaultChord);
    w.WriteBool("ChordYawMode", kDefaultChord);
    w.Close();
}

}

bool Config::LoadOrCreate(const char* iniPath) {
    if (!FileExists(iniPath)) {
        WriteDefaultIni(iniPath);
    }

    legacy::Config frozen;
    const legacy::ReadResult read = frozen.Read(iniPath);
    if (read.status == legacy::ReadStatus::Absent) {
        Log::Line("ERROR: Failed to open INI: %s", iniPath);
        return false;
    }
    if (read.status == legacy::ReadStatus::Refused) {
        return false;
    }

    enabled_on_startup = frozen.enabled_on_startup;
    udp_port = frozen.udp_port;
    sens_yaw = frozen.sens_yaw;
    sens_pitch = frozen.sens_pitch;
    sens_roll = frozen.sens_roll;
    invert_yaw = frozen.invert_yaw;
    invert_pitch = frozen.invert_pitch;
    invert_roll = frozen.invert_roll;
    local_smoothing = frozen.local_smoothing;
    remote_smoothing = frozen.remote_smoothing;
    deadzone_deg = frozen.deadzone_deg;
    data_freshness_ms = frozen.data_freshness_ms;
    world_space_yaw = frozen.world_space_yaw;
    position_enabled = frozen.position_enabled;
    lean_collision = frozen.lean_collision;
    lean_collision_skin_m = frozen.lean_collision_skin_m;
    camera_dump = frozen.camera_dump;
    reticle_probe = frozen.reticle_probe;
    vk_toggle = frozen.vk_toggle;
    vk_position = frozen.vk_position;
    vk_yaw_mode = frozen.vk_yaw_mode;
    chord_toggle = frozen.chord_toggle;
    chord_position = frozen.chord_position;
    chord_yaw_mode = frozen.chord_yaw_mode;
    return true;
}

}

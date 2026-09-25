#include "config.h"

#include "config_sanitize.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"

#include <windows.h>

#include <cstdio>
#include <fstream>

namespace DeusExHumanRevolutionHeadTracking {

namespace {

// Single source of truth for the INI defaults and the port validation bounds,
// shared by the writer (WriteDefaultIni) and the reader (LoadOrCreate) so the
// two cannot drift apart. The float-typed defaults widen to double for the
// WriteDouble calls and match ReadFloat exactly on the read side.
constexpr bool  kDefaultEnableOnStartup = true;
constexpr int   kDefaultPort            = 4242;
constexpr int   kMinPort                = 1024;
constexpr int   kMaxPort                = 65535;
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
constexpr cameraunlock::ads::AdsMode kDefaultAdsMode = cameraunlock::ads::kDefaultAdsMode;
constexpr bool  kDefaultCameraDump      = false;
constexpr bool  kDefaultReticleProbe    = false;
constexpr int   kDefaultVkToggle        = 0x23; // VK_END
constexpr int   kDefaultVkPosition      = 0x21; // VK_PRIOR (Page Up)
constexpr int   kDefaultVkYawMode       = 0x22; // VK_NEXT (Page Down)
constexpr int   kDefaultVkAds           = 0x2D; // VK_INSERT
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
    w.WriteComment(" What head tracking does while the sights are up. Cycled in game with Insert.");
    w.WriteComment("   paused  - tracking stands down until the weapon comes down (default)");
    w.WriteComment("   marker  - tracking stays live, and a white cross marks where rounds land");
    w.WriteComment("   tracked - tracking stays live, nothing drawn");
    w.WriteString("AdsMode", cameraunlock::ads::AdsModeValue(kDefaultAdsMode));
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
    w.WriteComment(" Virtual-key codes. Defaults: End (toggle), Page Up (cycle tracking mode), Page Down (yaw mode), Insert (ADS mode).");
    w.WriteHex("Toggle", kDefaultVkToggle);
    w.WriteHex("Position", kDefaultVkPosition);
    w.WriteHex("YawMode", kDefaultVkYawMode);
    w.WriteHex("Ads", kDefaultVkAds);
    w.WriteComment(" Chord alternatives: Ctrl+Shift+Y (toggle), Ctrl+Shift+G (cycle tracking mode), Ctrl+Shift+H (yaw mode), Ctrl+Shift+U (ADS mode).");
    w.WriteBool("ChordToggle", kDefaultChord);
    w.WriteBool("ChordPosition", kDefaultChord);
    w.WriteBool("ChordYawMode", kDefaultChord);
    w.WriteBool("ChordAds", kDefaultChord);
    w.Close();
}

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// Smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                             const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (reader.ReadString(section, key, "").empty()) return;
    warned = true;
    Log::Line(
        "WARN: Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

}

bool Config::LoadOrCreate(const char* iniPath) {
    if (!FileExists(iniPath)) {
        WriteDefaultIni(iniPath);
    }

    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        Log::Line("ERROR: Failed to open INI: %s", iniPath);
        return false;
    }

    enabled_on_startup = ini.ReadBool("General", "EnableOnStartup", kDefaultEnableOnStartup);
    int port = ini.ReadInt("General", "Port", kDefaultPort);
    if (port < kMinPort || port > kMaxPort) {
        Log::Line("ERROR: INI port %d out of range %d-%d", port, kMinPort, kMaxPort);
        return false;
    }
    udp_port = static_cast<uint16_t>(port);
    data_freshness_ms = ini.ReadInt("General", "DataFreshnessMs", kDefaultDataFreshnessMs);
    world_space_yaw = ini.ReadBool("General", "WorldSpaceYaw", kDefaultWorldSpaceYaw);
    position_enabled = ini.ReadBool("General", "PositionEnabled", kDefaultPositionEnabled);
    lean_collision = ini.ReadBool("General", "LeanCollision", kDefaultLeanCollision);
    // Anything that is not one of the three values lands on the default rather
    // than on whichever branch happens to be last. That covers a typo in a
    // hand-edited file, and it is also the migration path if a mode is ever
    // renamed: the player gets stock ADS rather than head tracking through their
    // sights that they never asked for.
    ads_mode = cameraunlock::ads::ParseAdsMode(
        ini.ReadString("General", "AdsMode",
                       cameraunlock::ads::AdsModeValue(kDefaultAdsMode)).c_str());
    camera_dump = ini.ReadBool("General", "CameraDump", kDefaultCameraDump);
    reticle_probe = ini.ReadBool("General", "ReticleProbe", kDefaultReticleProbe);

    auto sanitize = [](const char* name, float raw, float clean) {
        if (raw != clean) {
            Log::Line("WARN: INI %s value %.4f out of range or non-finite; using %.4f",
                      name, raw, clean);
        }
        return clean;
    };

    float rawSensYaw   = ini.ReadFloat("Sensitivity", "Yaw",   kDefaultSensitivity);
    float rawSensPitch = ini.ReadFloat("Sensitivity", "Pitch", kDefaultSensitivity);
    float rawSensRoll  = ini.ReadFloat("Sensitivity", "Roll",  kDefaultSensitivity);
    sens_yaw   = sanitize("Sensitivity.Yaw",   rawSensYaw,   SanitizeSensitivity(rawSensYaw));
    sens_pitch = sanitize("Sensitivity.Pitch", rawSensPitch, SanitizeSensitivity(rawSensPitch));
    sens_roll  = sanitize("Sensitivity.Roll",  rawSensRoll,  SanitizeSensitivity(rawSensRoll));
    invert_yaw   = ini.ReadBool("Sensitivity", "InvertYaw",   kDefaultInvert);
    invert_pitch = ini.ReadBool("Sensitivity", "InvertPitch", kDefaultInvert);
    invert_roll  = ini.ReadBool("Sensitivity", "InvertRoll",  kDefaultInvert);

    float rawLeanSkin = ini.ReadFloat("General", "LeanCollisionSkin", kDefaultLeanSkinM);
    lean_collision_skin_m = sanitize("General.LeanCollisionSkin", rawLeanSkin,
                                     SanitizeLeanSkin(rawLeanSkin, kDefaultLeanSkinM));

    float rawLocalSmoothing  = ini.ReadFloat("Smoothing", "LocalSmoothing",  kDefaultLocalSmoothing);
    float rawRemoteSmoothing = ini.ReadFloat("Smoothing", "RemoteSmoothing", kDefaultRemoteSmoothing);
    float rawDeadzone        = ini.ReadFloat("Smoothing", "DeadzoneDeg",     kDefaultDeadzoneDeg);
    local_smoothing  = sanitize("Smoothing.LocalSmoothing",  rawLocalSmoothing,
                                SanitizeSmoothing(rawLocalSmoothing, kDefaultLocalSmoothing));
    remote_smoothing = sanitize("Smoothing.RemoteSmoothing", rawRemoteSmoothing,
                                SanitizeSmoothing(rawRemoteSmoothing, kDefaultRemoteSmoothing));
    deadzone_deg     = sanitize("Smoothing.DeadzoneDeg",     rawDeadzone,        SanitizeDeadzone(rawDeadzone));

    WarnRetiredSmoothingKey(ini, "Smoothing", "Smoothing");

    vk_toggle   = ini.ReadHex("Hotkeys", "Toggle",   kDefaultVkToggle);
    vk_position = ini.ReadHex("Hotkeys", "Position", kDefaultVkPosition);
    vk_yaw_mode = ini.ReadHex("Hotkeys", "YawMode",  kDefaultVkYawMode);
    vk_ads      = ini.ReadHex("Hotkeys", "Ads",      kDefaultVkAds);
    chord_toggle   = ini.ReadBool("Hotkeys", "ChordToggle",   kDefaultChord);
    chord_position = ini.ReadBool("Hotkeys", "ChordPosition", kDefaultChord);
    chord_yaw_mode = ini.ReadBool("Hotkeys", "ChordYawMode",  kDefaultChord);
    chord_ads      = ini.ReadBool("Hotkeys", "ChordAds",      kDefaultChord);

    return true;
}

bool SaveAdsMode(const char* iniPath, cameraunlock::ads::AdsMode mode) {
    // Rewrites the one key in place, leaving the player's comments, spacing and
    // every other setting exactly as they were. A full rewrite of the file would
    // discard all three.
    return WritePrivateProfileStringA("General", "AdsMode",
                                      cameraunlock::ads::AdsModeValue(mode),
                                      iniPath) != FALSE;
}

}

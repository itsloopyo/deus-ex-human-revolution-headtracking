#include "legacy_config/legacy_config.h"

#include "legacy_config/config_sanitize.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"

namespace DeusExHumanRevolutionHeadTracking::legacy {

namespace {

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
constexpr bool  kDefaultCameraDump      = false;
constexpr bool  kDefaultReticleProbe    = false;
constexpr int   kDefaultVkToggle        = 0x23; // VK_END
constexpr int   kDefaultVkPosition      = 0x21; // VK_PRIOR (Page Up)
constexpr int   kDefaultVkYawMode       = 0x22; // VK_NEXT (Page Down)
constexpr bool  kDefaultChord           = true;

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

ReadResult Config::Read(const char* iniPath) {
    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        return {ReadStatus::Absent, {}};
    }

    enabled_on_startup = ini.ReadBool("General", "EnableOnStartup", kDefaultEnableOnStartup);
    int port = ini.ReadInt("General", "Port", kDefaultPort);
    if (port < kMinPort || port > kMaxPort) {
        Log::Line("ERROR: INI port %d out of range %d-%d", port, kMinPort, kMaxPort);
        return {ReadStatus::Refused, "[General] Port=" + std::to_string(port) + " is outside " +
                                         std::to_string(kMinPort) + "-" + std::to_string(kMaxPort)};
    }
    udp_port = static_cast<uint16_t>(port);
    data_freshness_ms = ini.ReadInt("General", "DataFreshnessMs", kDefaultDataFreshnessMs);
    world_space_yaw = ini.ReadBool("General", "WorldSpaceYaw", kDefaultWorldSpaceYaw);
    position_enabled = ini.ReadBool("General", "PositionEnabled", kDefaultPositionEnabled);
    lean_collision = ini.ReadBool("General", "LeanCollision", kDefaultLeanCollision);
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
    chord_toggle   = ini.ReadBool("Hotkeys", "ChordToggle",   kDefaultChord);
    chord_position = ini.ReadBool("Hotkeys", "ChordPosition", kDefaultChord);
    chord_yaw_mode = ini.ReadBool("Hotkeys", "ChordYawMode",  kDefaultChord);

    return {ReadStatus::Read, {}};
}

std::vector<cameraunlock::config::LegacyKey> ReadKeys() {
    return {
        {"General", "EnableOnStartup"},
        {"General", "Port"},
        {"General", "DataFreshnessMs"},
        {"General", "WorldSpaceYaw"},
        {"General", "PositionEnabled"},
        {"General", "LeanCollision"},
        {"General", "CameraDump"},
        {"General", "ReticleProbe"},
        {"Sensitivity", "Yaw"},
        {"Sensitivity", "Pitch"},
        {"Sensitivity", "Roll"},
        {"Sensitivity", "InvertYaw"},
        {"Sensitivity", "InvertPitch"},
        {"Sensitivity", "InvertRoll"},
        {"General", "LeanCollisionSkin"},
        {"Smoothing", "LocalSmoothing"},
        {"Smoothing", "RemoteSmoothing"},
        {"Smoothing", "DeadzoneDeg"},
        {"Smoothing", "Smoothing"},
        {"Hotkeys", "Toggle"},
        {"Hotkeys", "Position"},
        {"Hotkeys", "YawMode"},
        {"Hotkeys", "ChordToggle"},
        {"Hotkeys", "ChordPosition"},
        {"Hotkeys", "ChordYawMode"},
    };
}

}

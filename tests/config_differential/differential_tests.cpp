// The config differential test (convert-a-mod-to-the-canonical-config, section 5).
//
// Oracle: the reader of the newest published build, the rolling `dev` pre-release
// at 9a3d6ce, with the core sources it compiled at its pin bb4a0f6
// (oracle_adapter.h). Import: the frozen reader in src/legacy_config/.
//
// Migration: the conversion, run by the config owner on a copy of the input.
//
// Comparison 1, oracle against import, on every input: load status, every
// field both read (floats bit for bit), the startup state, and which actions
// every key press fires under every set of held modifiers. The one difference
// it may find is kComparison1Differences below.
//
// Comparison 2, import against migration, on every input: the same, where the
// only differences allowed are the approved drops the import records, and the
// deferral kUnrepresentable describes.
//
// The key presses go through the published build's own Hotkeys::Start and the
// guards it compiled (OracleFires), and through the guard the current build
// registers (CurrentFires).

#include "config.h"
#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace DeusExHumanRevolutionHeadTracking;

namespace {

// Comparison 1's one difference, present in every input: the published build
// also read [General] AdsMode, [Hotkeys] Ads and [Hotkeys] ChordAds, started in
// the ADS mode AdsMode named and registered the ADS cycle on Insert and
// Ctrl+Shift+U. 15eeb54 (feat: retire the ADS mode cycle, keep head tracking on
// through the aim) removed all of it before the conversion, so the import reads
// none of the three and registers no fourth action.
const char* const kComparison1Differences[] = {
    "[General] AdsMode, [Hotkeys] Ads, [Hotkeys] ChordAds: read by dev (9a3d6ce), not read since 15eeb54",
};

// Comparison 2's one departure from the rules. The published build read
// [General] DataFreshnessMs with no range, and a value below 1 (0 included,
// which is what text that is not a number reads as) left tracking permanently
// stale. The canonical row takes 1 to 2147483647 and core has no rule for a
// value outside it, so the owner defers such a file: it stays as it is, the
// session runs on what the import read, and nothing is saved.
const char* const kUnrepresentable =
    "[General] DataFreshnessMs below 1: not representable in the canonical row, so the conversion defers";

constexpr const char* kFileName = "DeusExHumanRevolutionHeadTracking.ini";

int g_failures = 0;
int g_checks = 0;
// How many inputs the migration ended in each load status, by status number.
int g_statuses[6] = {};

void Check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        if (g_failures < 200) std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("could not write " + path.string());
}

struct Listing {
    std::vector<std::pair<std::string, std::string>> files;
    bool operator==(const Listing& o) const { return files == o.files; }
};

Listing List(const fs::path& dir) {
    Listing l;
    for (const auto& e : fs::directory_iterator(dir)) {
        l.files.emplace_back(e.path().filename().string(), ReadBytes(e.path()));
    }
    std::sort(l.files.begin(), l.files.end());
    return l;
}

void SetReadOnly(const fs::path& path, bool readOnly) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("no attributes for " + path.string());
    const DWORD next = readOnly ? (attrs | FILE_ATTRIBUTE_READONLY) : (attrs & ~FILE_ATTRIBUTE_READONLY);
    if (!SetFileAttributesW(path.c_str(), next)) throw std::runtime_error("could not set attributes on " + path.string());
}

struct Input {
    std::string name;
    std::optional<std::string> bytes;  // nullopt: no file
};

// Startup state as dev:src/tracking_runtime.cpp Start derives it from the
// config: enabled from enabled_on_startup, RotationAndPosition when
// position_enabled else RotationOnly, the yaw mode from world_space_yaw.
struct Startup {
    bool enabled;
    int mode;  // cameraunlock::TrackingMode: 0 rotation and position, 1 rotation only
    bool world_space_yaw;
};

Startup StartupOf(const dxhr_oracle_view::OracleConfig& c) {
    return {c.enabled_on_startup, c.position_enabled ? 0 : 1, c.world_space_yaw};
}

Startup StartupOf(const legacy::Config& c) {
    return {c.enabled_on_startup, c.position_enabled ? 0 : 1, c.world_space_yaw};
}

bool SameStartup(const Startup& a, const Startup& b) {
    return a.enabled == b.enabled && a.mode == b.mode && a.world_space_yaw == b.world_space_yaw;
}

dxhr_oracle_view::HotkeyView KeysOf(const dxhr_oracle_view::OracleConfig& c) {
    return {c.vk_toggle, c.vk_position, c.vk_yaw_mode, c.chord_toggle, c.chord_position, c.chord_yaw_mode};
}

dxhr_oracle_view::HotkeyView KeysOf(const legacy::Config& c) {
    return {c.vk_toggle, c.vk_position, c.vk_yaw_mode, c.chord_toggle, c.chord_position, c.chord_yaw_mode};
}

cameraunlock::input::KeyModifiers g_currentHeld = cameraunlock::input::KeyModifiers::kNone;

cameraunlock::input::KeyModifiers CurrentHeld() { return g_currentHeld; }

cameraunlock::input::KeyModifiers ModifiersOf(int held) {
    using cameraunlock::input::KeyModifiers;
    KeyModifiers m = KeyModifiers::kNone;
    if ((held & 1) != 0) m = m | KeyModifiers::kCtrl;
    if ((held & 2) != 0) m = m | KeyModifiers::kShift;
    if ((held & 4) != 0) m = m | KeyModifiers::kAlt;
    return m;
}

// OracleFires' table for the current build. HEAD's Hotkeys::Start parses each
// key list and hands it to RegisterKeyBindings, which puts one
// detail::GuardKey callback per distinct key on the poller, holding that key's
// bindings in list order. The same callbacks are built here with the held
// modifiers read from the test rather than the keyboard, since the poller keeps
// its callbacks to itself.
dxhr_oracle_view::FireTable CurrentFires(const Config& m) {
    using dxhr_oracle_view::kFirstKey;
    using dxhr_oracle_view::kHeldStates;
    using dxhr_oracle_view::kLastKey;
    std::array<int, 3> fired{};
    std::vector<std::pair<int, std::function<void()>>> registered;
    const std::string* lists[3] = {&m.toggle_key_name, &m.cycle_tracking_mode_key_name, &m.yaw_mode_key_name};
    for (int action = 0; action < 3; ++action) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*lists[action]);
        if (!parsed.ok()) throw std::logic_error("migrated hotkey list '" + *lists[action] + "' does not parse");
        std::vector<int> keys;
        std::vector<std::vector<cameraunlock::input::KeyModifiers>> modifiers;
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            const auto at = std::find(keys.begin(), keys.end(), b.vk);
            if (at == keys.end()) {
                keys.push_back(b.vk);
                modifiers.push_back({b.modifiers});
            } else {
                modifiers[static_cast<std::size_t>(at - keys.begin())].push_back(b.modifiers);
            }
        }
        for (std::size_t i = 0; i < keys.size(); ++i) {
            registered.emplace_back(keys[i], cameraunlock::input::detail::GuardKey(
                                                 std::move(modifiers[i]), [&fired, action] { ++fired[action]; },
                                                 &CurrentHeld));
        }
    }

    dxhr_oracle_view::FireTable table;
    table.reserve((kLastKey - kFirstKey + 1) * kHeldStates);
    for (int vk = kFirstKey; vk <= kLastKey; ++vk) {
        for (int held = 0; held < kHeldStates; ++held) {
            fired = {};
            g_currentHeld = ModifiersOf(held);
            for (const auto& r : registered) {
                if (r.first == vk) r.second();
            }
            table.push_back(fired);
        }
    }
    g_currentHeld = cameraunlock::input::KeyModifiers::kNone;
    return table;
}

// The first press whose fired actions differ, for the failure message.
std::string FirstFireDifference(const dxhr_oracle_view::FireTable& expected, const dxhr_oracle_view::FireTable& got) {
    for (std::size_t i = 0; i < expected.size() && i < got.size(); ++i) {
        if (expected[i] != got[i]) {
            char text[160];
            std::snprintf(text, sizeof text, "key 0x%02X held %d fires %d/%d/%d, not %d/%d/%d",
                          static_cast<int>(i / dxhr_oracle_view::kHeldStates) + dxhr_oracle_view::kFirstKey,
                          static_cast<int>(i % dxhr_oracle_view::kHeldStates), got[i][0], got[i][1], got[i][2],
                          expected[i][0], expected[i][1], expected[i][2]);
            return text;
        }
    }
    return expected.size() == got.size() ? "none" : "the tables differ in size";
}

// Every field the import reads, against the oracle's field of the same name.
std::vector<std::string> FieldDifferences(const dxhr_oracle_view::OracleConfig& o, const legacy::Config& i) {
    std::vector<std::string> d;
    auto b = [&d](const char* n, bool x, bool y) { if (x != y) d.push_back(n); };
    auto f = [&d](const char* n, float x, float y) { if (!SameBits(x, y)) d.push_back(n); };
    auto n = [&d](const char* name, long long x, long long y) { if (x != y) d.push_back(name); };
    b("enabled_on_startup", o.enabled_on_startup, i.enabled_on_startup);
    n("udp_port", o.udp_port, i.udp_port);
    f("sens_yaw", o.sens_yaw, i.sens_yaw);
    f("sens_pitch", o.sens_pitch, i.sens_pitch);
    f("sens_roll", o.sens_roll, i.sens_roll);
    b("invert_yaw", o.invert_yaw, i.invert_yaw);
    b("invert_pitch", o.invert_pitch, i.invert_pitch);
    b("invert_roll", o.invert_roll, i.invert_roll);
    f("local_smoothing", o.local_smoothing, i.local_smoothing);
    f("remote_smoothing", o.remote_smoothing, i.remote_smoothing);
    f("deadzone_deg", o.deadzone_deg, i.deadzone_deg);
    n("data_freshness_ms", o.data_freshness_ms, i.data_freshness_ms);
    b("world_space_yaw", o.world_space_yaw, i.world_space_yaw);
    b("position_enabled", o.position_enabled, i.position_enabled);
    b("lean_collision", o.lean_collision, i.lean_collision);
    f("lean_collision_skin_m", o.lean_collision_skin_m, i.lean_collision_skin_m);
    b("camera_dump", o.camera_dump, i.camera_dump);
    b("reticle_probe", o.reticle_probe, i.reticle_probe);
    n("vk_toggle", o.vk_toggle, i.vk_toggle);
    n("vk_position", o.vk_position, i.vk_position);
    n("vk_yaw_mode", o.vk_yaw_mode, i.vk_yaw_mode);
    b("chord_toggle", o.chord_toggle, i.chord_toggle);
    b("chord_position", o.chord_position, i.chord_position);
    b("chord_yaw_mode", o.chord_yaw_mode, i.chord_yaw_mode);
    return d;
}

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

// The corpus descriptor of every key the frozen reader reads.
std::vector<cameraunlock::config::testing::MutationKey> MutationKeys() {
    using cameraunlock::config::testing::ChordSwitch;
    using cameraunlock::config::testing::MutationKey;
    auto plain = [](const char* s, const char* k, const char* alt, std::vector<std::string> oor = {}) {
        MutationKey m;
        m.section = s;
        m.key = k;
        m.alternate = alt;
        m.out_of_range = std::move(oor);
        return m;
    };
    auto hotkey = [](const char* k, const char* alt, const char* chord) {
        MutationKey m;
        m.section = "Hotkeys";
        m.key = k;
        m.alternate = alt;
        m.hotkey = true;
        m.chords.push_back(ChordSwitch{"Hotkeys", chord, "true", "false"});
        return m;
    };
    return {
        plain("General", "EnableOnStartup", "false"),
        plain("General", "Port", "4243", {"1023", "65536"}),
        plain("General", "DataFreshnessMs", "250"),
        plain("General", "WorldSpaceYaw", "false"),
        plain("General", "PositionEnabled", "false"),
        plain("General", "LeanCollision", "false"),
        plain("General", "CameraDump", "true"),
        plain("General", "ReticleProbe", "true"),
        plain("Sensitivity", "Yaw", "0.5"),
        plain("Sensitivity", "Pitch", "0.5"),
        plain("Sensitivity", "Roll", "0.5"),
        plain("Sensitivity", "InvertYaw", "true"),
        plain("Sensitivity", "InvertPitch", "true"),
        plain("Sensitivity", "InvertRoll", "true"),
        plain("General", "LeanCollisionSkin", "0.3", {"0.01", "0.6"}),
        plain("Smoothing", "LocalSmoothing", "0.3", {"-0.5", "1.5"}),
        plain("Smoothing", "RemoteSmoothing", "0.3", {"-0.5", "1.5"}),
        plain("Smoothing", "DeadzoneDeg", "1.5", {"-1.0"}),
        plain("Smoothing", "Smoothing", "0.3"),
        hotkey("Toggle", "0x70", "ChordToggle"),
        hotkey("Position", "0x71", "ChordPosition"),
        hotkey("YawMode", "0x72", "ChordYawMode"),
        plain("Hotkeys", "ChordToggle", "false"),
        plain("Hotkeys", "ChordPosition", "false"),
        plain("Hotkeys", "ChordYawMode", "false"),
    };
}

class Scratch {
public:
    Scratch() {
        root_ = fs::temp_directory_path() / ("dxhr-config-differential-" + std::to_string(GetCurrentProcessId()));
        fs::remove_all(root_);
        fs::create_directories(root_);
    }
    ~Scratch() {
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(root_, ec)) {
            if (e.is_regular_file()) SetFileAttributesW(e.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        fs::remove_all(root_, ec);
    }
    fs::path Fresh(const std::string& leaf) {
        const fs::path dir = root_ / (leaf + std::to_string(next_++));
        fs::create_directories(dir);
        return dir;
    }

private:
    fs::path root_;
    int next_ = 0;
};

fs::path Place(const fs::path& dir, const Input& input) {
    const fs::path file = dir / kFileName;
    if (input.bytes) WriteBytes(file, *input.bytes);
    return file;
}

struct ImportRun {
    legacy::Config config;
    legacy::ReadResult result;
};

// The import on a read-only copy of the input, which must leave its folder as
// it found it.
ImportRun RunImport(Scratch& scratch, const Input& input) {
    const fs::path dir = scratch.Fresh("import");
    const fs::path file = Place(dir, input);
    if (input.bytes) SetReadOnly(file, true);
    const Listing before = List(dir);
    ImportRun run;
    run.result = run.config.Read(file.string().c_str());
    Check(List(dir) == before, input.name + ": the import changed its folder");
    return run;
}

ImportRun Comparison1(Scratch& scratch, const Input& input) {
    const fs::path odir = scratch.Fresh("oracle");
    const dxhr_oracle_view::OracleResult oracle = dxhr_oracle_view::RunOracle(Place(odir, input).string());
    const ImportRun import = RunImport(scratch, input);

    const bool importUsable = import.result.status != legacy::ReadStatus::Refused;
    Check(oracle.loaded == importUsable, input.name + ": load status differs (oracle " +
                                             (oracle.loaded ? "loaded" : "refused") + ")");
    Check(input.bytes.has_value() || import.result.status == legacy::ReadStatus::Absent,
          input.name + ": no file is not Absent");
    if (oracle.loaded && importUsable) {
        const std::vector<std::string> fields = FieldDifferences(oracle.config, import.config);
        Check(fields.empty(), input.name + ": fields differ: " + Join(fields));
        Check(SameStartup(StartupOf(oracle.config), StartupOf(import.config)), input.name + ": startup state differs");
        const dxhr_oracle_view::FireTable oracleFires = dxhr_oracle_view::OracleFires(KeysOf(oracle.config));
        const dxhr_oracle_view::FireTable importFires = dxhr_oracle_view::OracleFires(KeysOf(import.config));
        Check(oracleFires == importFires,
              input.name + ": hotkeys fire differently: " + FirstFireDifference(oracleFires, importFires));
    }
    return import;
}

using cameraunlock::config::ConfigLoadStatus;
using cameraunlock::config::DropRule;
using cameraunlock::config::DroppedValue;
using cameraunlock::config::ImportResult;
using cameraunlock::config::ImportStatus;

cameraunlock::config::ConfigOwnerOptions<Config> OwnerOptions(const fs::path& file) {
    cameraunlock::config::ConfigOwnerOptions<Config> options;
    options.path = file.wstring();
    options.table = MakeConfigTable();
    options.import = MakeLegacyImport();
    options.header.display_name = kConfigDisplayName;
    return options;
}

// The import with its map, on its own copy, for the values it drops.
ImportResult RunMappedImport(Scratch& scratch, const Input& input) {
    const fs::path file = Place(scratch.Fresh("mapped"), input);
    cameraunlock::config::LegacyInput legacyInput;
    legacyInput.path = file.wstring();
    legacyInput.ansi_path = file.string();
    Config out;
    return MakeLegacyImport().run(legacyInput, out);
}

const DroppedValue* FindDrop(const std::vector<DroppedValue>& dropped, DropRule rule, const char* section,
                             const char* key) {
    for (const DroppedValue& d : dropped) {
        if (d.rule == rule && d.section == section && d.key == key) return &d;
    }
    return nullptr;
}

// N1 unbinds a nonzero code outside 0x01-0xFE, and the import must record it
// as dropped. The fire tables cover 0x01-0xFE only, so this is the one check
// that sees such a code. Code 0 never fired and is not recorded.
void CheckOutOfRangeDrop(int vk, const char* key, const std::vector<DroppedValue>& dropped,
                         const std::string& name) {
    const bool outOfRange = vk != 0 && (vk < 0x01 || vk > 0xFE);
    Check(outOfRange == (FindDrop(dropped, DropRule::KeyCodeOutOfRange, "Hotkeys", key) != nullptr),
          name + ": [Hotkeys] " + key + " dropped as out of range does not match its code");
}

void CheckPoseShaping(const ImportResult& imported, const char* section, const char* key, bool changed,
                      const std::string& name) {
    const std::string label = std::string("[") + section + "] " + key;
    const cameraunlock::config::PoseShapingValue* found = nullptr;
    for (const auto& p : imported.pose_shaping) {
        if (p.section == section && p.key == key) found = &p;
    }
    Check(found != nullptr, name + ": " + label + " is not recorded as pose shaping");
    if (found == nullptr) return;
    Check(found->folded == !changed, name + ": " + label + " folded does not match the player's value");
    Check((FindDrop(imported.dropped, DropRule::PoseShaping, section, key) != nullptr) == changed,
          name + ": " + label + " dropped does not match the player's value");
}

void Comparison2(Scratch& scratch, const Input& input, const ImportRun& import) {
    const fs::path file = Place(scratch.Fresh("migration"), input);
    std::optional<cameraunlock::config::ConfigLoadResult<Config>> loaded;
    {
        cameraunlock::config::ConfigOwner<Config> owner(OwnerOptions(file));
        loaded = owner.Load();
    }
    const fs::path copy = fs::path(file.wstring() + L".pre-canonical");
    ++g_statuses[static_cast<int>(loaded->status)];

    bool deferred = false;
    if (!input.bytes) {
        Check(loaded->status == ConfigLoadStatus::Created, input.name + ": no file is not Created");
    } else if (import.result.status == legacy::ReadStatus::Refused) {
        Check(loaded->status == ConfigLoadStatus::LegacyRefused, input.name + ": a refused file is not LegacyRefused");
        Check(ReadBytes(file) == *input.bytes, input.name + ": a refused file was changed");
        Check(!fs::exists(copy), input.name + ": a refused file got a copy");
        return;
    } else if (import.config.data_freshness_ms < 1) {
        // The session runs on the Config the deferral hands back, so everything
        // below up to the converted-file checks applies to it too.
        Check(loaded->status == ConfigLoadStatus::Deferred, input.name + ": " + kUnrepresentable + ", not deferred");
        Check(ReadBytes(file) == *input.bytes, input.name + ": a deferred file was changed");
        Check(!fs::exists(copy), input.name + ": a deferred file got a copy");
        if (loaded->status != ConfigLoadStatus::Deferred) return;
        deferred = true;
    } else {
        Check(loaded->status == ConfigLoadStatus::Migrated,
              input.name + ": not Migrated but " + cameraunlock::config::ConfigLoadStatusName(loaded->status));
        if (loaded->status != ConfigLoadStatus::Migrated) return;
        Check(ReadBytes(copy) == *input.bytes, input.name + ": .pre-canonical is not the input");
    }

    const legacy::Config& l = import.config;
    const Config& m = loaded->config;
    std::vector<std::string> d;
    if (m.enable_on_startup != l.enabled_on_startup) d.push_back("EnableOnStartup");
    if (m.udp_port != l.udp_port) d.push_back("UdpPort");
    if (m.data_freshness_ms != l.data_freshness_ms) d.push_back("DataFreshnessMs");
    if (m.world_space_yaw != l.world_space_yaw) d.push_back("WorldSpaceYaw");
    const auto mode = cameraunlock::DecodeTrackingMode(m.rotation_enabled, m.position_enabled);
    if (!mode || *mode != (l.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                                              : cameraunlock::TrackingMode::RotationOnly)) {
        d.push_back("tracking mode");
    }
    if (!SameBits(m.local_smoothing, l.local_smoothing) || !SameBits(m.position.local_smoothing, l.local_smoothing)) {
        d.push_back("LocalSmoothing");
    }
    if (!SameBits(m.remote_smoothing, l.remote_smoothing) ||
        !SameBits(m.position.remote_smoothing, l.remote_smoothing)) {
        d.push_back("RemoteSmoothing");
    }
    if (m.collision_enabled != l.lean_collision) d.push_back("CollisionEnabled");
    if (!SameBits(m.lean_clamp.skin, l.lean_collision_skin_m)) d.push_back("CollisionMargin");
    if (m.camera_dump != l.camera_dump) d.push_back("CameraDump");
    Check(d.empty(), input.name + ": migration differs from the import: " + Join(d));

    // The running mod keeps the processor's identity sensitivity, inversion and
    // deadzone, and always places the reticle on the aim. Each departure from
    // that in the import must be an approved drop.
    const ImportResult imported = RunMappedImport(scratch, input);
    Check(imported.status == (input.bytes ? ImportStatus::Imported : ImportStatus::Absent),
          input.name + ": the mapped import's status");
    CheckPoseShaping(imported, "Sensitivity", "Yaw", !SameBits(l.sens_yaw, 1.0f), input.name);
    CheckPoseShaping(imported, "Sensitivity", "Pitch", !SameBits(l.sens_pitch, 1.0f), input.name);
    CheckPoseShaping(imported, "Sensitivity", "Roll", !SameBits(l.sens_roll, 1.0f), input.name);
    CheckPoseShaping(imported, "Sensitivity", "InvertYaw", l.invert_yaw, input.name);
    CheckPoseShaping(imported, "Sensitivity", "InvertPitch", l.invert_pitch, input.name);
    CheckPoseShaping(imported, "Sensitivity", "InvertRoll", l.invert_roll, input.name);
    CheckPoseShaping(imported, "Smoothing", "DeadzoneDeg", !SameBits(l.deadzone_deg, 0.0f), input.name);
    Check((FindDrop(imported.dropped, DropRule::Reticle, "General", "ReticleProbe") != nullptr) == l.reticle_probe,
          input.name + ": ReticleProbe dropped does not match the player's value");
    for (const DroppedValue& drop : imported.dropped) {
        Check(drop.rule == DropRule::PoseShaping || drop.rule == DropRule::Reticle ||
                  drop.rule == DropRule::KeyCodeOutOfRange,
              input.name + ": unexpected drop " + cameraunlock::config::DescribeDroppedValue(drop));
    }

    CheckOutOfRangeDrop(l.vk_toggle, "Toggle", imported.dropped, input.name);
    CheckOutOfRangeDrop(l.vk_position, "Position", imported.dropped, input.name);
    CheckOutOfRangeDrop(l.vk_yaw_mode, "YawMode", imported.dropped, input.name);
    const dxhr_oracle_view::FireTable before = dxhr_oracle_view::OracleFires(KeysOf(l));
    const dxhr_oracle_view::FireTable after = CurrentFires(m);
    Check(before == after, input.name + ": hotkeys fire differently: " + FirstFireDifference(before, after));

    if (deferred) return;

    // The converted file: the reader and the table find nothing to report,
    // rendering what was read gives the same bytes, and the next launch
    // converts nothing.
    const std::string bytes = ReadBytes(file);
    const cameraunlock::config::CanonicalIni doc = cameraunlock::config::ParseCanonicalIni(bytes);
    Config reread;
    const cameraunlock::config::ApplyReport report = cameraunlock::config::ApplyCanonical(doc, MakeConfigTable(), reread);
    Check(doc.IsReadable() && doc.diagnostics.empty() && report.diagnostics.empty(),
          input.name + ": the converted file draws diagnostics");
    Check(cameraunlock::config::RenderCanonical(MakeConfigTable(), reread,
                                                cameraunlock::config::RenderHeader{kConfigDisplayName}) == bytes,
          input.name + ": rendering the re-read file gives other bytes");
    {
        cameraunlock::config::ConfigOwner<Config> again(OwnerOptions(file));
        Check(again.Load().status == ConfigLoadStatus::Canonical, input.name + ": the converted file converted again");
    }
    Check(ReadBytes(file) == bytes, input.name + ": the next launch rewrote the converted file");
    Check(!fs::exists(fs::path(file.wstring() + L".pre-canonical.last")),
          input.name + ": the next launch kept another copy");
}

std::vector<Input> Inputs(const std::string& firstRun) {
    using cameraunlock::config::testing::GenerateIniMutations;
    std::vector<Input> inputs;
    inputs.push_back({"no file", std::nullopt});
    inputs.push_back({"empty file", std::string()});
    inputs.push_back({"dev first-run output", firstRun});
    for (auto& m : GenerateIniMutations(firstRun, legacy::ReadKeys(), MutationKeys())) {
        inputs.push_back({"corpus: " + m.name, std::move(m.bytes)});
    }
    return inputs;
}

}  // namespace

int main() {
    try {
        Scratch scratch;

        // The dev build's first-run output, committed once as test data, is what
        // the oracle still writes for a missing file.
        const std::string firstRun = ReadBytes(fs::path(DXHR_DIFFERENTIAL_DATA) / "dev-first-run.ini");
        Check(!firstRun.empty(), "data/dev-first-run.ini is missing");
        {
            const fs::path dir = scratch.Fresh("first-run");
            const fs::path file = dir / kFileName;
            Check(dxhr_oracle_view::RunOracle(file.string()).loaded, "the oracle did not load its own first run");
            Check(ReadBytes(file) == firstRun, "the oracle's first-run output differs from data/dev-first-run.ini");
        }

        const std::vector<Input> inputs = Inputs(firstRun);
        std::printf("comparison 1 (oracle dev 9a3d6ce against the import) on %zu inputs\n", inputs.size());
        for (const char* d : kComparison1Differences) std::printf("  recorded difference: %s\n", d);
        std::printf("comparison 2 (the import against the migration)\n");
        std::printf("  recorded departure: %s\n", kUnrepresentable);
        for (const Input& input : inputs) Comparison2(scratch, input, Comparison1(scratch, input));
    } catch (const std::exception& e) {
        std::printf("  FAIL: threw: %s\n", e.what());
        ++g_failures;
    }

    for (int i = 0; i < 6; ++i) {
        if (g_statuses[i] != 0) {
            std::printf("  migration %s: %d inputs\n",
                        cameraunlock::config::ConfigLoadStatusName(static_cast<ConfigLoadStatus>(i)), g_statuses[i]);
        }
    }
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

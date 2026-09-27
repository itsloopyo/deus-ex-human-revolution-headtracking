// The config differential test (convert-a-mod-to-the-canonical-config, section 5).
//
// Oracle: the reader of the newest published build, the rolling `dev` pre-release
// at 9a3d6ce, with the core sources it compiled at its pin bb4a0f6
// (oracle_adapter.h). Import: the frozen reader in src/legacy_config/.
//
// Migration: the config owner's Load in a folder holding only the input as the
// legacy file, which imports it into a new CameraUnlock.ini, then CameraUnlock.ini
// read back by a second Load. Every owner reads one scratch Defaults.ini at the
// built-in values, outside the folder.
//
// Comparison 1, oracle against import, on every input: load status, every
// field both read (floats bit for bit), the startup state, and which actions
// every key press fires under every set of held modifiers. The one difference
// it may find is kComparison1Differences below.
//
// Comparison 2, import against migration, on every input: the same, where the
// only differences allowed are the approved drops the import records, and the
// deferral kUnrepresentable describes. Also asserted: the legacy file keeps its
// bytes, its last write time and a read-only attribute, a read-only copy imports
// as a writable one does, the folder holds nothing but the legacy file and
// CameraUnlock.ini, and a second load reads CameraUnlock.ini, gives the same
// settings and changes neither file.
//
// A row the player never changed from what dev shipped follows Defaults.ini:
// the import lists it in follows_defaults_ini and the migration writes it
// `default`, the tracking mode pair as one unit. The test derives the untouched
// rows from what the import read and holds the import's list to them on every
// input; the dev first-run output, the empty file and no file list every row
// and migrate to the committed file byte for byte. Each input with a file also
// migrates over a Defaults.ini that differs from the built-in values on every
// row, where an untouched row takes Defaults.ini's value and a changed row
// keeps the player's. Each distinct CameraUnlock.ini the
// migration wrote goes to DXHR_MIGRATED_DIR, which lint-migrated.mjs then holds
// to core's canonical config lint.
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

#include <map>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <set>
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
// value outside it, so the owner defers such a file: CameraUnlock.ini is not
// created, the session runs on what the import read, and nothing is saved.
const char* const kUnrepresentable =
    "[General] DataFreshnessMs below 1: not representable in the canonical row, so the conversion defers";

int g_failures = 0;
int g_checks = 0;
// How many inputs the migration ended in each load status, by status number.
int g_statuses[6] = {};
// Every distinct CameraUnlock.ini the migration wrote, for the lint.
std::set<std::string> g_migrated;
// Inputs that left every row at dev's default, and that changed the tracking mode.
int g_allUntouched = 0;
int g_modeChanged = 0;
// Hotkey codes on a modifier key alone, which N3 unbinds.
int g_modifierCodes = 0;

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
    // Removes every Fresh folder so far, keeping Defaults.ini. Called after each
    // input, so the corpus never has thousands of folders on disk at once.
    void Clear() {
        for (const auto& e : fs::recursive_directory_iterator(root_)) {
            if (e.is_regular_file()) SetFileAttributesW(e.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        for (const auto& e : fs::directory_iterator(root_)) {
            if (e.path().filename() != "global" && e.path().filename() != "skewed") fs::remove_all(e.path());
        }
    }
    fs::path Fresh(const std::string& leaf) {
        const fs::path dir = root_ / (leaf + std::to_string(next_++));
        fs::create_directories(dir);
        return dir;
    }
    // Outside every Fresh folder. The first load creates it with the built-in values.
    fs::path DefaultsPath() const { return root_ / "global" / "Defaults.ini"; }
    // Where main writes kSkewedDefaults, outside every Fresh folder.
    fs::path SkewedDefaultsPath() const { return root_ / "skewed" / "Defaults.ini"; }

private:
    fs::path root_;
    int next_ = 0;
};

fs::path Place(const fs::path& dir, const Input& input) {
    const fs::path file = dir / kLegacyFileName;
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

using cameraunlock::config::ConfigLoadResult;
using cameraunlock::config::ConfigLoadStatus;
using cameraunlock::config::DefaultsFile;
using cameraunlock::config::DropRule;
using cameraunlock::config::DroppedValue;
using cameraunlock::config::ImportResult;
using cameraunlock::config::ImportStatus;

ConfigLoadResult<Config> LoadFolder(Scratch& scratch, const fs::path& folder) {
    cameraunlock::config::ConfigOwner<Config> owner(
        MakeOwnerOptions(folder, DefaultsFile::At(scratch.DefaultsPath().wstring())));
    return owner.Load();
}

using cameraunlock::config::schema::Concept;

// The settings kSkewedDefaults gives, set in main.
Config g_skewed;

ConfigLoadResult<Config> LoadFolder(const fs::path& folder, const fs::path& defaults) {
    cameraunlock::config::ConfigOwner<Config> owner(MakeOwnerOptions(folder, DefaultsFile::At(defaults.wstring())));
    return owner.Load();
}

// Every row the table binds that follows Defaults.ini. CollisionMargin keeps
// the game's own value and CameraDump is the mod's own.
const std::set<Concept>& GlobalRows() {
    static const std::set<Concept> rows = {
        Concept::UdpPort,          Concept::EnableOnStartup,           Concept::WorldSpaceYaw,
        Concept::RotationEnabled,  Concept::PositionEnabled,           Concept::DataFreshnessMs,
        Concept::LocalSmoothing,   Concept::RemoteSmoothing,           Concept::CollisionEnabled,
        Concept::CollisionReleaseSmoothing, Concept::ToggleKey,        Concept::CycleTrackingModeKey,
        Concept::YawModeKey,       Concept::TrueFreeLook,              Concept::TrueFreeLookKey,
    };
    return rows;
}

// The rows the player never changed from dev's defaults, the mode pair as one
// unit. CollisionReleaseSmoothing, TrueFreeLook and TrueFreeLookKey had no key,
// so no player changed them.
std::set<Concept> UntouchedRows(const legacy::Config& l) {
    const legacy::Config d;
    std::set<Concept> changed;
    if (l.udp_port != d.udp_port) changed.insert(Concept::UdpPort);
    if (l.enabled_on_startup != d.enabled_on_startup) changed.insert(Concept::EnableOnStartup);
    if (l.world_space_yaw != d.world_space_yaw) changed.insert(Concept::WorldSpaceYaw);
    if (l.position_enabled != d.position_enabled) {
        changed.insert(Concept::RotationEnabled);
        changed.insert(Concept::PositionEnabled);
    }
    if (l.data_freshness_ms != d.data_freshness_ms) changed.insert(Concept::DataFreshnessMs);
    if (!SameBits(l.local_smoothing, d.local_smoothing)) changed.insert(Concept::LocalSmoothing);
    if (!SameBits(l.remote_smoothing, d.remote_smoothing)) changed.insert(Concept::RemoteSmoothing);
    if (l.lean_collision != d.lean_collision) changed.insert(Concept::CollisionEnabled);
    if (l.vk_toggle != d.vk_toggle || l.chord_toggle != d.chord_toggle) changed.insert(Concept::ToggleKey);
    if (l.vk_position != d.vk_position || l.chord_position != d.chord_position) {
        changed.insert(Concept::CycleTrackingModeKey);
    }
    if (l.vk_yaw_mode != d.vk_yaw_mode || l.chord_yaw_mode != d.chord_yaw_mode) changed.insert(Concept::YawModeKey);
    std::set<Concept> untouched;
    for (const Concept row : GlobalRows()) {
        if (changed.count(row) == 0) untouched.insert(row);
    }
    return untouched;
}

std::string Names(const std::set<Concept>& rows) {
    std::string text;
    for (const Concept row : rows) {
        text += (text.empty() ? "" : ", ") + std::string(cameraunlock::config::schema::kConcepts[static_cast<std::size_t>(row)].name);
    }
    return text.empty() ? "none" : text;
}

// `row`'s fields copied from `from` into `to`.
void CopyRow(Concept row, const Config& from, Config& to) {
    switch (row) {
        case Concept::UdpPort: to.udp_port = from.udp_port; break;
        case Concept::EnableOnStartup: to.enable_on_startup = from.enable_on_startup; break;
        case Concept::WorldSpaceYaw: to.world_space_yaw = from.world_space_yaw; break;
        case Concept::RotationEnabled: to.rotation_enabled = from.rotation_enabled; break;
        case Concept::PositionEnabled: to.position_enabled = from.position_enabled; break;
        case Concept::DataFreshnessMs: to.data_freshness_ms = from.data_freshness_ms; break;
        case Concept::LocalSmoothing:
            to.local_smoothing = from.local_smoothing;
            to.position.local_smoothing = from.position.local_smoothing;
            break;
        case Concept::RemoteSmoothing:
            to.remote_smoothing = from.remote_smoothing;
            to.position.remote_smoothing = from.position.remote_smoothing;
            break;
        case Concept::CollisionEnabled: to.collision_enabled = from.collision_enabled; break;
        case Concept::CollisionReleaseSmoothing: to.lean_clamp.release_smoothing = from.lean_clamp.release_smoothing; break;
        case Concept::ToggleKey: to.toggle_key_name = from.toggle_key_name; break;
        case Concept::CycleTrackingModeKey: to.cycle_tracking_mode_key_name = from.cycle_tracking_mode_key_name; break;
        case Concept::YawModeKey: to.yaw_mode_key_name = from.yaw_mode_key_name; break;
        case Concept::TrueFreeLook: to.true_free_look = from.true_free_look; break;
        case Concept::TrueFreeLookKey: to.true_free_look_key_name = from.true_free_look_key_name; break;
        default: throw std::logic_error("no fields for a row the table does not bind");
    }
}

std::string RenderValues(const Config& c);

// A Defaults.ini other than the built-in values on every row in GlobalRows,
// the tracking mode pair taken together.
const char* const kSkewedDefaults =
    "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n"
    "[Network]\r\nUdpPort=5353\r\n\r\n"
    "[General]\r\nEnableOnStartup=false\r\nWorldSpaceYaw=false\r\nRotationEnabled=false\r\nDataFreshnessMs=750\r\n\r\n"
    "[Smoothing]\r\nLocalSmoothing=0.5\r\nRemoteSmoothing=0.45\r\n\r\n"
    "[Position]\r\nPositionEnabled=true\r\nCollisionEnabled=false\r\nCollisionReleaseSmoothing=0.4\r\n"
    "TrueFreeLook=true\r\n\r\n"
    "[Hotkeys]\r\nToggleKey=F8\r\nCycleTrackingModeKey=F9\r\nYawModeKey=F10\r\nTrueFreeLookKey=F11\r\n";

// The settings the skewed Defaults.ini gives every row in GlobalRows.
Config SkewedConfig() {
    const cameraunlock::config::CanonicalIni doc = cameraunlock::config::ParseCanonicalIni(kSkewedDefaults);
    Config c;
    const cameraunlock::config::ApplyReport report = cameraunlock::config::ApplyCanonical(doc, MakeConfigTable(), c);
    if (!doc.IsReadable() || !doc.diagnostics.empty() || !report.diagnostics.empty()) {
        throw std::logic_error("the skewed Defaults.ini draws diagnostics");
    }
    const Config builtin;
    for (const Concept row : GlobalRows()) {
        Config probe = builtin;
        CopyRow(row, c, probe);
        if (row == Concept::RotationEnabled || row == Concept::PositionEnabled) {
            CopyRow(Concept::RotationEnabled, c, probe);
            CopyRow(Concept::PositionEnabled, c, probe);
        }
        if (RenderValues(probe) == RenderValues(builtin)) {
            throw std::logic_error(std::string("the skewed Defaults.ini leaves ") +
                                   cameraunlock::config::schema::kConcepts[static_cast<std::size_t>(row)].name +
                                   " at the built-in value");
        }
    }
    return c;
}

// Every row the table binds, as the renderer writes it: two configs that
// render the same hold the same settings.
std::string RenderValues(const Config& c) {
    return cameraunlock::config::RenderCanonical(MakeConfigTable(), c,
                                                 cameraunlock::config::RenderHeader{kConfigDisplayName});
}

// RenderValues for two configs, one of which may hold the DataFreshnessMs the
// renderer refuses (kUnrepresentable): that field is compared on its own, and
// every other row by rendering.
bool SameSettings(const Config& a, const Config& b) {
    Config x = a, y = b;
    if (x.data_freshness_ms != y.data_freshness_ms) return false;
    x.data_freshness_ms = y.data_freshness_ms = 1;
    return RenderValues(x) == RenderValues(y);
}

struct FileState {
    std::string bytes;
    fs::file_time_type written;
    bool read_only;
};

FileState StateOf(const fs::path& file) {
    const DWORD attrs = GetFileAttributesW(file.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("no attributes for " + file.string());
    return {ReadBytes(file), fs::last_write_time(file), (attrs & FILE_ATTRIBUTE_READONLY) != 0};
}

bool SameState(const FileState& a, const FileState& b) {
    return a.bytes == b.bytes && a.written == b.written && a.read_only == b.read_only;
}

std::vector<std::string> FileNames(const fs::path& dir) {
    std::vector<std::string> names;
    for (const auto& e : fs::directory_iterator(dir)) names.push_back(e.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

// One migration of `input` in a fresh folder, the legacy file read-only or not.
struct Migration {
    fs::path folder;
    ConfigLoadResult<Config> loaded;
    std::optional<FileState> legacyBefore;
    std::optional<FileState> legacyAfter;
    std::vector<std::string> files;
};

Migration Migrate(Scratch& scratch, const Input& input, bool readOnly) {
    Migration m;
    m.folder = scratch.Fresh(readOnly ? "migration-ro" : "migration");
    const fs::path legacy = Place(m.folder, input);
    if (input.bytes) {
        if (readOnly) SetReadOnly(legacy, true);
        m.legacyBefore = StateOf(legacy);
    }
    m.loaded = LoadFolder(scratch, m.folder);
    if (input.bytes) m.legacyAfter = StateOf(legacy);
    m.files = FileNames(m.folder);
    return m;
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
    const Migration writable = Migrate(scratch, input, false);
    const ConfigLoadResult<Config>& loaded = writable.loaded;
    ++g_statuses[static_cast<int>(loaded.status)];

    const std::vector<std::string> legacyOnly{kLegacyFileName};
    const std::vector<std::string> both{kConfigFileName, kLegacyFileName};
    if (input.bytes) {
        Check(SameState(*writable.legacyBefore, *writable.legacyAfter),
              input.name + ": the load changed the legacy file's bytes, write time or attributes");
        const Migration readOnly = Migrate(scratch, input, true);
        Check(SameState(*readOnly.legacyBefore, *readOnly.legacyAfter) && readOnly.legacyAfter->read_only,
              input.name + ": the load changed a read-only legacy file");
        Check(readOnly.loaded.status == loaded.status && readOnly.files == writable.files &&
                  SameSettings(readOnly.loaded.config, loaded.config),
              input.name + ": a read-only legacy file does not import as a writable one does");
        if (readOnly.files == both && writable.files == both) {
            Check(ReadBytes(readOnly.folder / kConfigFileName) == ReadBytes(writable.folder / kConfigFileName),
                  input.name + ": a read-only legacy file imports into other bytes");
        }
    }

    bool deferred = false;
    if (!input.bytes) {
        Check(loaded.status == ConfigLoadStatus::Created, input.name + ": no file is not Created");
        Check(writable.files == std::vector<std::string>{kConfigFileName},
              input.name + ": Created left another file beside CameraUnlock.ini");
    } else if (import.result.status == legacy::ReadStatus::Refused) {
        Check(loaded.status == ConfigLoadStatus::LegacyRefused, input.name + ": a refused file is not LegacyRefused");
        Check(writable.files == legacyOnly, input.name + ": a refused import created a file");
        return;
    } else if (import.config.data_freshness_ms < 1) {
        // The session runs on the Config the deferral hands back, so everything
        // below up to the CameraUnlock.ini checks applies to it too.
        Check(loaded.status == ConfigLoadStatus::Deferred, input.name + ": " + kUnrepresentable + ", not deferred");
        Check(writable.files == legacyOnly, input.name + ": a deferred import created a file");
        if (loaded.status != ConfigLoadStatus::Deferred) return;
        deferred = true;
    } else {
        Check(loaded.status == ConfigLoadStatus::Migrated,
              input.name + ": not Migrated but " + cameraunlock::config::ConfigLoadStatusName(loaded.status) + ": " +
                  loaded.reason);
        if (loaded.status != ConfigLoadStatus::Migrated) return;
        Check(writable.files == both, input.name + ": the import left a file other than CameraUnlock.ini");
    }

    const legacy::Config& l = import.config;
    const Config& m = loaded.config;
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
    // The published build read no key for it and ran its lean clamp at
    // LeanClampSettings' default release smoothing, 0.9 at core bb4a0f6.
    if (!SameBits(m.lean_clamp.release_smoothing, 0.9f)) {
        d.push_back("CollisionReleaseSmoothing");
    }
    if (m.camera_dump != l.camera_dump) d.push_back("CameraDump");
    // The published build had no true free look: the lean stays eased out
    // through the aim, as it was, and the toggle takes core's keys.
    if (m.true_free_look) d.push_back("TrueFreeLook");
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
                  drop.rule == DropRule::KeyCodeOutOfRange || drop.rule == DropRule::ModifierKey,
              input.name + ": unexpected drop " + cameraunlock::config::DescribeDroppedValue(drop));
    }

    CheckOutOfRangeDrop(l.vk_toggle, "Toggle", imported.dropped, input.name);
    CheckOutOfRangeDrop(l.vk_position, "Position", imported.dropped, input.name);
    CheckOutOfRangeDrop(l.vk_yaw_mode, "YawMode", imported.dropped, input.name);
    // N3 unbinds a legacy code on a Ctrl, Shift or Alt key alone and records it;
    // the chord stays. Apart from that the keys fire as the published build's did.
    legacy::Config unbound = l;
    for (auto [vk, key] : {std::pair<int*, const char*>{&unbound.vk_toggle, "Toggle"},
                           {&unbound.vk_position, "Position"}, {&unbound.vk_yaw_mode, "YawMode"}}) {
        const bool modifier = (*vk >= 0x10 && *vk <= 0x12) || (*vk >= 0xA0 && *vk <= 0xA5);
        Check(modifier == (FindDrop(imported.dropped, DropRule::ModifierKey, "Hotkeys", key) != nullptr),
              input.name + ": [Hotkeys] " + key + " dropped as a modifier key does not match its code");
        if (modifier) {
            *vk = 0;
            ++g_modifierCodes;
        }
    }
    const dxhr_oracle_view::FireTable before = dxhr_oracle_view::OracleFires(KeysOf(unbound));
    const dxhr_oracle_view::FireTable after = CurrentFires(m);
    Check(before == after, input.name + ": hotkeys fire differently: " + FirstFireDifference(before, after));

    const std::set<Concept> follows(imported.follows_defaults_ini.begin(), imported.follows_defaults_ini.end());
    Check(follows.size() == imported.follows_defaults_ini.size(), input.name + ": follows_defaults_ini names a row twice");
    const std::set<Concept> untouched = UntouchedRows(l);
    Check(follows == untouched, input.name + ": follows Defaults.ini " + Names(follows) + ", the player left " +
                                    Names(untouched) + " untouched");
    if (untouched == GlobalRows()) ++g_allUntouched;
    if (untouched.count(Concept::RotationEnabled) == 0) ++g_modeChanged;
    if (!input.bytes || input.name == "empty file" || input.name == "dev first-run output") {
        Check(untouched == GlobalRows(), input.name + ": a file no player edited leaves a row changed");
    }

    if (deferred) return;

    // Over a Defaults.ini that differs everywhere, an untouched row takes its
    // value and a changed row keeps the player's.
    if (input.bytes) {
        const fs::path folder = scratch.Fresh("skewed");
        Place(folder, input);
        const ConfigLoadResult<Config> skewed = LoadFolder(folder, scratch.SkewedDefaultsPath());
        Check(skewed.status == ConfigLoadStatus::Migrated, input.name + ": over the skewed Defaults.ini, not Migrated");
        Config want = m;
        for (const Concept row : follows) CopyRow(row, g_skewed, want);
        Check(RenderValues(skewed.config) == RenderValues(want),
              input.name + ": over the skewed Defaults.ini, the untouched rows do not take its values or the changed "
                           "rows lose the player's");
    }

    // CameraUnlock.ini: the reader and the table find nothing to report, and the
    // next launch reads it, over the same Defaults.ini, into the same settings
    // without importing and without writing either file.
    const fs::path file = writable.folder / kConfigFileName;
    const std::string bytes = ReadBytes(file);
    const cameraunlock::config::CanonicalIni doc = cameraunlock::config::ParseCanonicalIni(bytes);
    Config reread;
    const cameraunlock::config::ApplyReport report = cameraunlock::config::ApplyCanonical(doc, MakeConfigTable(), reread);
    Check(doc.IsReadable() && doc.diagnostics.empty() && report.diagnostics.empty(),
          input.name + ": CameraUnlock.ini draws diagnostics");
    g_migrated.insert(bytes);
    for (const Concept row : follows) {
        const std::string key = cameraunlock::config::schema::kConcepts[static_cast<std::size_t>(row)].key;
        Check(bytes.find("\r\n" + key + "=default\r\n") != std::string::npos,
              input.name + ": " + key + " is not written default");
    }
    if (!input.bytes || input.name == "empty file" || input.name == "dev first-run output") {
        Check(bytes == ReadBytes(DXHR_COMMITTED_CONFIG), input.name + ": does not give the committed file byte for byte");
    }

    const fs::file_time_type written = fs::last_write_time(file);
    const ConfigLoadResult<Config> again = LoadFolder(scratch, writable.folder);
    Check(again.status == ConfigLoadStatus::Canonical, input.name + ": the second load did not read CameraUnlock.ini");
    Check(RenderValues(again.config) == RenderValues(m), input.name + ": the second load gives other settings");
    Check(ReadBytes(file) == bytes && fs::last_write_time(file) == written,
          input.name + ": the second load rewrote CameraUnlock.ini");
    if (input.bytes) {
        Check(SameState(*writable.legacyBefore, StateOf(writable.folder / kLegacyFileName)),
              input.name + ": the second load changed the legacy file");
    }
    Check(FileNames(writable.folder) == writable.files, input.name + ": the second load left another file");
}

// Each distinct CameraUnlock.ini the migration wrote, for lint-migrated.mjs.
void WriteMigrated() {
    const fs::path dir = DXHR_MIGRATED_DIR;
    fs::remove_all(dir);
    fs::create_directories(dir);
    int n = 0;
    for (const std::string& bytes : g_migrated) {
        char name[32];
        std::snprintf(name, sizeof name, "%04d.ini", n++);
        WriteBytes(dir / name, bytes);
    }
    std::printf("  %d distinct CameraUnlock.ini files written for the lint\n", n);
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
            const fs::path file = dir / kLegacyFileName;
            Check(dxhr_oracle_view::RunOracle(file.string()).loaded, "the oracle did not load its own first run");
            Check(ReadBytes(file) == firstRun, "the oracle's first-run output differs from data/dev-first-run.ini");
        }

        g_skewed = SkewedConfig();
        fs::create_directories(scratch.SkewedDefaultsPath().parent_path());
        WriteBytes(scratch.SkewedDefaultsPath(), kSkewedDefaults);

        const std::vector<Input> inputs = Inputs(firstRun);
        std::printf("comparison 1 (oracle dev 9a3d6ce against the import) on %zu inputs\n", inputs.size());
        for (const char* d : kComparison1Differences) std::printf("  recorded difference: %s\n", d);
        std::printf("comparison 2 (the import against the migration)\n");
        std::printf("  recorded departure: %s\n", kUnrepresentable);
        for (const Input& input : inputs) {
            Comparison2(scratch, input, Comparison1(scratch, input));
            scratch.Clear();
        }
        WriteMigrated();
        std::printf("  %d inputs left every row at dev's default, %d changed the tracking mode\n", g_allUntouched,
                    g_modeChanged);
        Check(g_allUntouched > 0 && g_modeChanged > 0 && g_allUntouched < static_cast<int>(inputs.size()),
              "the inputs both leave rows untouched and change them, the tracking mode among them");
        Check(g_modifierCodes > 0, "the corpus reaches a hotkey code on a modifier key alone");
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

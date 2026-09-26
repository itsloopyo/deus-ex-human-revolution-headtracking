// The committed config, the upgrade from the published build's file and the
// owner's saves.
//
// `--render-config <path>` writes the table's fresh render to <path> and exits
// without running the tests; `pixi run render-config` uses it to rewrite the
// committed file after a change to a row, a comment or a default.

#include "config.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace DeusExHumanRevolutionHeadTracking;
using cameraunlock::config::ConfigLoadStatus;
using cameraunlock::config::ConfigOwner;
using cameraunlock::config::ConfigSaveResult;
using cameraunlock::config::ConfigSaveStatus;
using cameraunlock::config::DefaultsFile;

namespace {

// The repo path core's data/config-format.json records as `committed`. It keeps
// the legacy file's name; the mod creates the same bytes as CameraUnlock.ini.
constexpr const char* kCommittedConfig = "DeusExHumanRevolutionHeadTracking.ini";

int g_failures = 0;

void Check(bool cond, const std::string& what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("could not read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("could not write " + path.string());
}

// The file the owner creates at first launch, and the committed file.
std::string Rendered() {
    return cameraunlock::config::RenderCanonicalFresh(MakeConfigTable(),
                                                      cameraunlock::config::RenderHeader{kConfigDisplayName});
}

bool LogHas(const std::vector<std::string>& log, const std::string& text) {
    for (const std::string& line : log) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

std::vector<std::string> FileNames(const fs::path& dir) {
    std::vector<std::string> names;
    for (const auto& e : fs::directory_iterator(dir)) names.push_back(e.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i + 1 < bytes.size(); ++i) {
        if (bytes[i] == '\r' && bytes[i + 1] == '\n') {
            lines.push_back(bytes.substr(start, i - start));
            start = i + 2;
        }
    }
    lines.push_back(bytes.substr(start));
    return lines;
}

// Every line that differs, as "before -> after". The two files must have the
// same number of lines.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before), b = Lines(after);
    if (a.size() != b.size()) return {"line count changed"};
    std::vector<std::string> changed;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(a[i] + " -> " + b[i]);
    }
    return changed;
}

// A scratch game folder per case, and one Defaults.ini outside all of them,
// created by the first load with the built-in values.
class Scratch {
public:
    Scratch() {
        root_ = fs::temp_directory_path() / ("dxhr-config-tests-" + std::to_string(GetCurrentProcessId()));
        fs::remove_all(root_);
        fs::create_directories(root_);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    fs::path Folder(const std::string& leaf) {
        const fs::path dir = root_ / leaf;
        fs::create_directories(dir);
        return dir;
    }
    fs::path DefaultsPath() const { return root_ / "global" / "Defaults.ini"; }
    ConfigOwner<Config> Owner(const fs::path& folder) const {
        return ConfigOwner<Config>(MakeOwnerOptions(folder, DefaultsFile::At(DefaultsPath().wstring())));
    }

private:
    fs::path root_;
};

void RenderTest(const std::string& committed) {
    std::printf("render\n");
    Check(Rendered() == committed,
          "the committed config differs from the table's fresh render; run pixi run render-config");
    const cameraunlock::config::CanonicalIni doc = cameraunlock::config::ParseCanonicalIni(committed);
    Check(doc.IsReadable() && doc.diagnostics.empty(), "the committed file draws reader diagnostics");
    Config read;
    const cameraunlock::config::ApplyReport report = cameraunlock::config::ApplyCanonical(doc, MakeConfigTable(), read);
    Check(report.diagnostics.empty(), "the committed file draws table diagnostics");
}

// The newest published build wrote this file on its first run. It carries no
// setting away from its default, so with Defaults.ini at the built-in values it
// imports into the committed file. It is the only input of this kind: no build
// shipped a config or seeded one.
void FreshEqualsUpgrade(Scratch& scratch, const std::string& committed, const std::string& firstRun) {
    std::printf("fresh equals upgrade\n");
    const fs::path created = scratch.Folder("created");
    Check(scratch.Owner(created).Load().status == ConfigLoadStatus::Created, "no file is not Created");
    Check(ReadBytes(created / kConfigFileName) == committed, "the created file is not the committed file");
    Check(FileNames(created) == std::vector<std::string>{kConfigFileName},
          "Created left another file beside CameraUnlock.ini");

    const fs::path upgraded = scratch.Folder("upgraded");
    const fs::path legacy = upgraded / kLegacyFileName;
    WriteBytes(legacy, firstRun);
    const fs::file_time_type written = fs::last_write_time(legacy);
    Check(scratch.Owner(upgraded).Load().status == ConfigLoadStatus::Migrated,
          "the dev first-run output is not Migrated");
    Check(ReadBytes(upgraded / kConfigFileName) == committed,
          "the dev first-run output does not import into the committed file");
    Check(ReadBytes(legacy) == firstRun && fs::last_write_time(legacy) == written,
          "the import changed the legacy file");
    Check(FileNames(upgraded) == std::vector<std::string>{kConfigFileName, kLegacyFileName},
          "the import left a file other than CameraUnlock.ini beside the legacy file");

    const cameraunlock::config::ConfigLoadResult<Config> again = scratch.Owner(upgraded).Load();
    Check(again.status == ConfigLoadStatus::Canonical, "CameraUnlock.ini does not load as Canonical");
    Check(LogHas(again.log, "is left as it was and is not read"),
          "the second load does not log that the legacy file is not read");
    Check(ReadBytes(upgraded / kConfigFileName) == committed, "loading CameraUnlock.ini rewrote it");
    Check(ReadBytes(legacy) == firstRun && fs::last_write_time(legacy) == written,
          "the second load changed the legacy file");
}

void CheckSaved(const ConfigSaveResult& saved, const std::string& what) {
    Check(saved.status == ConfigSaveStatus::Saved, what + " failed: " + saved.reason);
}

// A save starts from the committed file's default rows and writes only the
// rows it changed, each as a value.
void SaveTests(Scratch& scratch, const std::string& committed) {
    std::printf("saves\n");
    const fs::path folder = scratch.Folder("saves");
    const fs::path file = folder / kConfigFileName;
    WriteBytes(file, committed);
    ConfigOwner<Config> owner = scratch.Owner(folder);
    Check(owner.Load().status == ConfigLoadStatus::Canonical, "the committed file does not load as Canonical");
    const std::string defaultsBefore = ReadBytes(scratch.DefaultsPath());

    std::string before = ReadBytes(file);
    const ConfigSaveResult yaw = owner.Save([](Config& c) { c.world_space_yaw = false; });
    CheckSaved(yaw, "the yaw mode save");
    std::vector<std::string> changed = ChangedLines(before, ReadBytes(file));
    Check(changed.size() == 1 && changed[0] == "WorldSpaceYaw=default -> WorldSpaceYaw=false",
          "the yaw mode save changed more than its line");
    Check(LogHas(yaw.log, "WorldSpaceYaw=false is now set for this game, and no longer follows Defaults.ini"),
          "the yaw mode save does not log that the row stopped following Defaults.ini");

    before = ReadBytes(file);
    CheckSaved(owner.Save([](Config& c) {
                   c.rotation_enabled = true;
                   c.position_enabled = false;
               }),
               "the rotation-only save");
    changed = ChangedLines(before, ReadBytes(file));
    Check(changed.size() == 2 && changed[0] == "RotationEnabled=default -> RotationEnabled=true" &&
              changed[1] == "PositionEnabled=default -> PositionEnabled=false",
          "the rotation-only save did not write exactly the pair");

    before = ReadBytes(file);
    CheckSaved(owner.Save([](Config& c) {
                   c.rotation_enabled = false;
                   c.position_enabled = true;
               }),
               "the position-only save");
    changed = ChangedLines(before, ReadBytes(file));
    Check(changed.size() == 2 && changed[0] == "RotationEnabled=true -> RotationEnabled=false" &&
              changed[1] == "PositionEnabled=false -> PositionEnabled=true",
          "the position-only save did not change exactly the pair");

    before = ReadBytes(file);
    CheckSaved(owner.Save([](Config&) {}), "an empty save");
    Check(ReadBytes(file) == before, "an empty save wrote the file");

    bool threw = false;
    try {
        owner.Save([](Config& c) { c.enable_on_startup = false; });
    } catch (const std::logic_error&) {
        threw = true;
    }
    Check(threw, "EnableOnStartup is Writable: End must never persist");
    Check(ReadBytes(file) == before, "a refused save wrote the file");
    Check(ReadBytes(scratch.DefaultsPath()) == defaultsBefore, "a save changed Defaults.ini");

    const cameraunlock::config::ConfigLoadResult<Config> loaded = scratch.Owner(folder).Load();
    Check(!loaded.config.world_space_yaw && !loaded.config.rotation_enabled && loaded.config.position_enabled,
          "the saved toggles did not come back at the next load");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
            WriteBytes(argv[2], Rendered());
            std::printf("wrote %s\n", argv[2]);
            return 0;
        }
        if (argc != 1) {
            std::printf("usage: %s [--render-config <path>]\n", argv[0]);
            return 2;
        }

        const std::string committed = ReadBytes(fs::path(DXHR_REPO_ROOT) / kCommittedConfig);
        const std::string firstRun =
            ReadBytes(fs::path(DXHR_REPO_ROOT) / "tests/config_differential/data/dev-first-run.ini");
        Scratch scratch;
        RenderTest(committed);
        FreshEqualsUpgrade(scratch, committed, firstRun);
        SaveTests(scratch, committed);
    } catch (const std::exception& e) {
        std::printf("  FAIL: threw: %s\n", e.what());
        ++g_failures;
    }
    std::printf("%d failures\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}

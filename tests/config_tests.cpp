// The committed config and the owner's saves.
//
// `--render-config <path>` writes the table's defaults, rendered, to <path> and
// exits without running the tests; `pixi run render-config` uses it to rewrite
// the committed file after a change to a row, a comment or a default.

#include "config.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"

#include <windows.h>

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
using cameraunlock::config::ConfigOwnerOptions;
using cameraunlock::config::ConfigSaveStatus;

namespace {

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

std::string Rendered() {
    const cameraunlock::config::ConfigTable<Config> table = MakeConfigTable();
    return cameraunlock::config::RenderCanonical(table, table.defaults(),
                                                 cameraunlock::config::RenderHeader{kConfigDisplayName});
}

ConfigOwnerOptions<Config> Options(const fs::path& file) {
    ConfigOwnerOptions<Config> options;
    options.path = file.wstring();
    options.table = MakeConfigTable();
    options.import = MakeLegacyImport();
    options.header.display_name = kConfigDisplayName;
    return options;
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
    fs::path Fresh(const std::string& leaf) {
        const fs::path dir = root_ / leaf;
        fs::create_directories(dir);
        return dir / kConfigFileName;
    }

private:
    fs::path root_;
};

void RenderTest(const std::string& committed) {
    std::printf("render\n");
    Check(Rendered() == committed, std::string(kConfigFileName) +
                                       " differs from the table's defaults; run pixi run render-config");
    const cameraunlock::config::CanonicalIni doc = cameraunlock::config::ParseCanonicalIni(committed);
    Check(doc.IsReadable() && doc.diagnostics.empty(), "the committed file draws reader diagnostics");
    Config read;
    const cameraunlock::config::ApplyReport report = cameraunlock::config::ApplyCanonical(doc, MakeConfigTable(), read);
    Check(report.diagnostics.empty(), "the committed file draws table diagnostics");
}

// The newest published build wrote this file on its first run. It carries no
// setting away from its default, so it converts to the committed file. It is
// the only input of this kind: no build shipped a config or seeded one.
void FreshEqualsUpgrade(Scratch& scratch, const std::string& committed, const std::string& firstRun) {
    std::printf("fresh equals upgrade\n");
    const fs::path created = scratch.Fresh("created");
    ConfigOwner<Config> fresh(Options(created));
    Check(fresh.Load().status == ConfigLoadStatus::Created, "no file is not Created");
    Check(ReadBytes(created) == committed, "the created file is not the committed file");

    const fs::path upgraded = scratch.Fresh("upgraded");
    WriteBytes(upgraded, firstRun);
    ConfigOwner<Config> upgrade(Options(upgraded));
    Check(upgrade.Load().status == ConfigLoadStatus::Migrated, "the dev first-run output is not Migrated");
    Check(ReadBytes(upgraded) == committed, "the dev first-run output does not convert to the committed file");
    Check(ReadBytes(fs::path(upgraded.wstring() + L".pre-canonical")) == firstRun,
          ".pre-canonical does not hold the dev first-run output");

    ConfigOwner<Config> again(Options(upgraded));
    Check(again.Load().status == ConfigLoadStatus::Canonical, "the converted file does not load as Canonical");
    Check(ReadBytes(upgraded) == committed, "loading the converted file rewrote it");
}

void SaveTests(Scratch& scratch, const std::string& committed) {
    std::printf("saves\n");
    const fs::path file = scratch.Fresh("saves");
    WriteBytes(file, committed);
    ConfigOwner<Config> owner(Options(file));
    Check(owner.Load().status == ConfigLoadStatus::Canonical, "the committed file does not load as Canonical");

    std::string before = ReadBytes(file);
    Check(owner.Save([](Config& c) { c.world_space_yaw = false; }).status == ConfigSaveStatus::Saved,
          "the yaw mode save failed");
    std::vector<std::string> changed = ChangedLines(before, ReadBytes(file));
    Check(changed.size() == 1 && changed[0] == "WorldSpaceYaw=true -> WorldSpaceYaw=false",
          "the yaw mode save changed more than its line");

    before = ReadBytes(file);
    Check(owner.Save([](Config& c) {
              c.rotation_enabled = true;
              c.position_enabled = false;
          }).status == ConfigSaveStatus::Saved,
          "the tracking mode save failed");
    changed = ChangedLines(before, ReadBytes(file));
    Check(changed.size() == 1 && changed[0] == "PositionEnabled=true -> PositionEnabled=false",
          "the rotation-only save changed more than its line");

    before = ReadBytes(file);
    Check(owner.Save([](Config& c) {
              c.rotation_enabled = false;
              c.position_enabled = true;
          }).status == ConfigSaveStatus::Saved,
          "the position-only save failed");
    changed = ChangedLines(before, ReadBytes(file));
    Check(changed.size() == 2 && changed[0] == "RotationEnabled=true -> RotationEnabled=false" &&
              changed[1] == "PositionEnabled=false -> PositionEnabled=true",
          "the position-only save did not change exactly the pair");

    before = ReadBytes(file);
    Check(owner.Save([](Config&) {}).status == ConfigSaveStatus::Saved, "an empty save failed");
    Check(ReadBytes(file) == before, "an empty save wrote the file");

    bool threw = false;
    try {
        owner.Save([](Config& c) { c.enable_on_startup = false; });
    } catch (const std::logic_error&) {
        threw = true;
    }
    Check(threw, "EnableOnStartup is Writable: End must never persist");
    Check(ReadBytes(file) == before, "a refused save wrote the file");

    ConfigOwner<Config> restarted(Options(file));
    const cameraunlock::config::ConfigLoadResult<Config> loaded = restarted.Load();
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

        const std::string committed = ReadBytes(fs::path(DXHR_REPO_ROOT) / kConfigFileName);
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

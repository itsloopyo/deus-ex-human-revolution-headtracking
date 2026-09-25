#include "hotkeys.h"

#include "logging.h"

#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace DeusExHumanRevolutionHeadTracking {

namespace {

// The config table already refused a list that does not parse, so one here is
// a bug rather than a player's typo.
std::vector<cameraunlock::input::KeyBinding> Parse(const std::string& list) {
    cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) throw std::logic_error("hotkey list '" + list + "' does not parse: " + parsed.error);
    return parsed.bindings;
}

}

bool Hotkeys::Start(const Config& cfg, Action onToggle,
                    Action onCycleMode, Action onYawMode) {
    if (m_started) return true;

    using cameraunlock::input::RegisterKeyBindings;
    RegisterKeyBindings(m_poller, Parse(cfg.toggle_key_name), std::move(onToggle));
    RegisterKeyBindings(m_poller, Parse(cfg.cycle_tracking_mode_key_name), std::move(onCycleMode));
    RegisterKeyBindings(m_poller, Parse(cfg.yaw_mode_key_name), std::move(onYawMode));

    if (!m_poller.Start(16)) {
        Log::Line("ERROR: HotkeyPoller failed to start");
        return false;
    }

    Log::Line("Hotkeys: toggle=[%s] cycle mode=[%s] yaw mode=[%s]",
              cfg.toggle_key_name.c_str(), cfg.cycle_tracking_mode_key_name.c_str(),
              cfg.yaw_mode_key_name.c_str());

    m_started = true;
    return true;
}

void Hotkeys::Stop() {
    if (!m_started) return;
    m_poller.Stop();
    m_started = false;
}

}

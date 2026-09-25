#include "hotkeys.h"

#include "logging.h"

#include "cameraunlock/input/chord_hotkeys.h"

namespace DeusExHumanRevolutionHeadTracking {

bool Hotkeys::Start(const Config& cfg, Action onToggle,
                    Action onCycleMode, Action onYawMode, Action onAdsMode) {
    if (m_started) return true;

    using cameraunlock::input::NavGuarded;
    using cameraunlock::input::ChordGuarded;

    // Nav-cluster bindings. Suppressed when Ctrl+Shift is held so the chord
    // path (below) is the sole trigger for Ctrl+Shift+<nav> combos.
    m_poller.SetToggleKey(cfg.vk_toggle, NavGuarded(onToggle));
    m_poller.AddHotkey(cfg.vk_position, NavGuarded(onCycleMode));
    m_poller.AddHotkey(cfg.vk_yaw_mode, NavGuarded(onYawMode));
    m_poller.AddHotkey(cfg.vk_ads, NavGuarded(onAdsMode));

    // Ctrl+Shift+<letter> chord bindings (Y/G/H/U cluster); ChordGuarded
    // gates each action on the modifier state.
    if (cfg.chord_toggle)   m_poller.AddHotkey('Y', ChordGuarded(std::move(onToggle)));
    if (cfg.chord_position) m_poller.AddHotkey('G', ChordGuarded(std::move(onCycleMode)));
    if (cfg.chord_yaw_mode) m_poller.AddHotkey('H', ChordGuarded(std::move(onYawMode)));
    if (cfg.chord_ads)      m_poller.AddHotkey('U', ChordGuarded(std::move(onAdsMode)));

    if (!m_poller.Start(16)) {
        Log::Line("ERROR: HotkeyPoller failed to start");
        return false;
    }

    Log::Line("Hotkeys: toggle=0x%02X cyclemode=0x%02X yawmode=0x%02X ads=0x%02X chords=%d/%d/%d/%d",
              cfg.vk_toggle, cfg.vk_position, cfg.vk_yaw_mode, cfg.vk_ads,
              cfg.chord_toggle ? 1 : 0,
              cfg.chord_position ? 1 : 0, cfg.chord_yaw_mode ? 1 : 0,
              cfg.chord_ads ? 1 : 0);

    m_started = true;
    return true;
}

void Hotkeys::Stop() {
    if (!m_started) return;
    m_poller.Stop();
    m_started = false;
}

}

#pragma once

#include "config.h"

#include "cameraunlock/input/hotkey_poller.h"

#include <functional>

namespace DeusExHumanRevolutionHeadTracking {

class Hotkeys {
public:
    using Action = std::function<void()>;

    bool Start(const Config& cfg, Action onToggle,
               Action onCycleMode, Action onYawMode, Action onAdsMode);
    void Stop();

private:
    cameraunlock::input::HotkeyPoller m_poller;
    bool m_started = false;
};

}

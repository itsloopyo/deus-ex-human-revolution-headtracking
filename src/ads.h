#pragma once

#include "build_profile.h"

namespace DeusExHumanRevolutionHeadTracking {

// Whether the player has the sights up, this frame.
//
// Recomputed from the game's own player state rather than latched off an
// enter/leave edge: the two input callbacks that raise and lower the sights are
// not the only paths into and out of that state, and a flag that misses one edge
// either strands the player in ADS behaviour or leaks hip-fire tracking into an
// aim. A frame the game did not publish - a menu, a loading screen, a cinematic,
// where the player controller stops ticking - reads as NOT aiming, which fails
// towards stock.
bool SightsAreUp();

// Hooks the iron-sight controller's per-frame update so the state above has
// somewhere to come from. The controller is ticked by NsPlayerController::Update,
// which early-returns off gameplay, so the hook stops firing exactly where the
// answer stops being knowable.
class AdsHook {
public:
    bool Install(const BuildProfile& profile);
    void Uninstall();

private:
    bool m_installed = false;
};

}  // namespace DeusExHumanRevolutionHeadTracking

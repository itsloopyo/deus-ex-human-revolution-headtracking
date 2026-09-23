#pragma once

#include "build_profile.h"
#include "config.h"

namespace DeusExHumanRevolutionHeadTracking {

// Head tracking decouples aim from view, which leaves the game's crosshair
// marking the middle of the screen rather than the place a shot lands. The
// reticle is a Scaleform movie of its own, so instead of hiding it and drawing
// a replacement, this shifts that movie to the screen position where the clean
// aim point falls in the head-tracked view. The game's own reticle art, spread
// animation and hit markers keep working untouched.
class ReticleHook {
public:
    bool Install(const BuildProfile& profile, const Config& cfg);
    void Uninstall();

private:
    bool m_installed = false;
};

}  // namespace DeusExHumanRevolutionHeadTracking

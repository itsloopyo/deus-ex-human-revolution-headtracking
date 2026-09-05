#pragma once

#include "build_profile.h"

#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/math/vec3.h"

// The engine half of the lean collision clamp: ask the game how far the eye may
// travel from where the game put it before it meets something solid.
//
// Core owns what to do with the answer (cameraunlock/camera/lean_clamp.h); this
// owns getting one. The sweep is the engine's OWN line trace - the same
// singleton method, the same query kind and the same argument shape the game
// uses for its weapon trace and for its camera collision - so there is no second
// physics world in this mod to disagree with the game's.
namespace DeusExHumanRevolutionHeadTracking {
namespace lean_trace {

// Records the module base and the profile's collision RVAs. Call once, after
// the build profile has matched.
void Install(const BuildProfile& profile);

// The w of the camera transform's position row, set by the camera hook each
// frame. The engine reads its trace endpoints with MOVAPS, whole vec4s, and the
// start point IS that row: handing back the w the game put there is the
// difference between passing the engine its own point and passing it a guess.
void SetEyeW(float w);

// The near clip distance the renderer built this frame's projection from, in
// world units, or 0 when it cannot be read.
//
// This is the floor the lean standoff has to clear. Geometry closer to the eye
// than the near plane is not drawn, so an eye stopped short of a wall but still
// inside the near plane sees straight through it - the wall opens into a
// polygonal cutaway and the clamp looks like it is not working at all.
float NearPlaneUnits();

// The query, in the shape core's clamp takes. `context` is unused - everything
// the trace needs comes from the game's own globals. Game thread only, and only
// from inside the camera update, where the collision world is the one the frame
// is being built against.
cameraunlock::camera::LeanObstruction Query(void* context,
                                            const cameraunlock::math::Vec3& start,
                                            const cameraunlock::math::Vec3& direction,
                                            float maxDistance);

}  // namespace lean_trace
}  // namespace DeusExHumanRevolutionHeadTracking

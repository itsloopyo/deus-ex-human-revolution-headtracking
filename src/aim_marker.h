#pragma once

namespace DeusExHumanRevolutionHeadTracking {

// The mark that says where the rounds are going while the sights are up.
//
// AdsMode::Marker draws it; the other two modes do not. The projection is NOT
// here: the caller hands over the screen position the reticle compensation
// already computed, because two projections drift apart and only one of them can
// be right.
//
// Wrapped rather than used directly so that the graphics headers are confined to
// one translation unit.

// Brings the overlay up the first time it is called and answers whether it is
// drawing yet. Safe to call from a rendered frame - the install happens on a
// worker thread - and safe to call every frame. Call it only from the mode that
// wants the marker, so a player who never asks for one never has their swap
// chain patched.
//
// False means the marker cannot draw, and the marker mode then behaves exactly
// like AdsMode::Tracked - the honest degradation, because a mark that
// half-draws is worse than none.
bool EnsureAimMarker();

// The aim in the drawn frame, x right, y up, -1..1. `visible` is a per-frame
// decision and is never latched: an invalid projection publishes false rather
// than leaving the last mark standing where the rounds are not going.
void PublishAimMarker(bool visible, float ndcX, float ndcY);

}  // namespace DeusExHumanRevolutionHeadTracking

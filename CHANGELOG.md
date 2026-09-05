# Changelog

## [Unreleased]

### Added

- Head tracking now moves the view by the same amount on screen whatever the
  game has done with its field of view. Popping out of cover, raising the
  sights and putting a scope up all zoom in, and a zoom magnifies head tracking
  along with everything else in the frame - 1.3x at the sights and 2.4x through
  a scope, which reads as the mod's sensitivity jumping the moment you aim. The
  pose is now scaled by the ratio between the field of view being rendered and
  the game's own 90 degree walking-around one, so a head movement carries the
  view as far zoomed in as it does zoomed out. Head tilt is left alone, because
  a tilt rotates the picture rather than moving it and looks the same at any
  zoom. There is nothing to configure and the scaling is exactly 1.0 whenever
  the game is not zoomed.

- Aim-down-sights handling, on `Insert` / `Ctrl+Shift+U`. Three modes, cycled
  with the key and saved to the INI as `AdsMode`: tracking paused for the
  duration of the aim (the default, and indistinguishable from an unmodded
  game), tracking live with a white cross drawn wherever the rounds will land,
  and tracking live with nothing drawn. All three ease onto the aim over 150ms
  and back off over 250ms rather than switching, and the two tracked modes feed
  poses measured from the frame the sights came up on, so raising the weapon
  puts the view on the point the reticle was marking whatever angle your head
  is holding. Head tilt is left alone throughout - it moves no aim point, and
  levelling it would jolt the horizon twice per aim.
- The white aim marker draws at the same screen position the reticle
  compensation already computes, through an overlay on the game's Direct3D 11
  swap chain. One projection, used twice, so the mark and the reticle cannot
  disagree about where the shot goes. It is authoritative over a scope's own
  reticle, which is only honest while the eye sits exactly on the optic.
- Lean collision, on by default. The eye is held off walls, doors and cover
  instead of passing through them: before the lean is added to the camera the
  mod asks the game's own collision world what stands between the eye and where
  the tracker wants it, and shortens the lean to fit. Contact is immediate and
  the release is eased, so pressing into cover stops the view dead at the
  surface and coming off it does not pop. Nothing the game reads changes - the
  question is asked about the clean camera, so aim, projectiles and the
  interaction pick are all untouched. Two INI keys under `[General]`:
  `LeanCollision` to turn it off, and `LeanCollisionSkin` (metres, default
  0.19) for how far off a surface to hold the eye. The standoff is held clear of
  the camera's near clip plane, which the mod reads out of the frame's own
  projection rather than assuming: this game floors that plane at 40 world
  units (12.7cm), and a standoff inside it stops the eye short of the wall while
  the wall itself is still too close to be drawn, so it opens into a polygonal
  cutaway. A configured value below the plane is raised to clear it and the log
  says so. An INI written by an earlier version does not carry the keys; add
  them by hand or delete the file to have the defaults rewritten.
- 6DOF position tracking. Leaning moves the eye through the world, resolved
  against the camera's clean axes so a lean follows the body rather than the
  head-turned view, and scaled by the engine's own 316.05 units-per-metre
  constant.

### Changed

- The startup `Graphics runtime:` log line now says what it actually reports -
  which graphics DLLs are mapped a fraction of a second into the process - and
  says plainly that it is not the renderer. It read `d3d9=1 d3d11=0` on a game
  that renders through Direct3D 11 from start to finish.

### Fixed

- The reticle no longer drifts in the direction of head movement. The engine's
  world-to-screen reads the camera object as it stands, and the reticle's update
  did not always run while that held the head-tracked transform - on 40% of
  frames it held the clean one, the offset collapsed to zero and the reticle sat
  at screen centre, carried along by the head. The projection now uses the basis
  captured for the frame, so it no longer depends on that timing, and a one-shot
  check against the engine's own projection warns if a future build changes the
  basis layout or the FOV convention.
- Head rotation is applied to the camera basis instead of its transpose. The
  engine stores the basis in the view matrix's columns, not its rows, so the
  previous build turned a transposed camera and the view did not track.
- The view matrix's translation row is recomputed from the turned basis. It
  encodes the camera position projected onto the camera axes, so leaving the
  game's row in place while the basis turned slid the eye across the level as
  the player moved their head.

### Changed

- Strip the third-party DLLs Ultimate ASI Loader carries as resources out of
  the vendored copy. The upstream 32-bit build embeds `binkw32.dll` (RAD Game
  Tools' Bink and Smacker 1.994i, proprietary middleware licensed per title),
  `wndmode.dll` (DirectX Windower Embedded, (C) 2008 VEG and (C) 2004 menopem,
  no licence) and `vorbisfile.dll` (Xiph.Org, BSD-3-Clause) so that a user who
  renames the loader over one of those libraries still gets the original
  exports. The installer ZIP ships that binary, so it was redistributing all
  three. `scripts/strip-loader-payload.ps1` now zeroes them, `pixi run
  update-deps` runs it on every refresh, and `pixi run package` refuses to
  build a ZIP from a loader that still has them. Only the `.rsrc` section
  changes: the loader's code, imports, relocations and appended PDB are
  byte-identical to upstream, and nothing in this mod could reach the stripped
  resources anyway.
- `THIRD-PARTY-NOTICES.md` recorded cameraunlock-core at a commit the submodule
  no longer points at; it is restamped to the commit the mod compiles.
- Removed mod-side recentring. The tracker app owns the centre, so the mod now
  applies the pose it receives as absolute. The `Home` / `Ctrl+Shift+T` binding
  and the `[Hotkeys] Recenter` and `[Hotkeys] ChordRecenter` INI keys are gone.
  Centre in your tracker app instead.
- The log is now `HeadTracking.log` next to the game EXE (was
  `DeusExHumanRevolutionHeadTracking.log`), and keeps one previous generation.
  Each launch renames the existing log to `HeadTracking.prev.log` and opens a
  fresh one, so a session never grows across launches and a crash report
  written on the way down survives the relaunch that follows it. A rename that
  fails is reported in the fresh log, so a stale `.prev.log` is never mistaken
  for the last session.
- The `CameraDump` discovery mode now stops after 40 dumps instead of dumping
  11 lines every 5 seconds for the whole session.
- The log now states the port the UDP receiver bound to on success, not only on
  failure, so "is the receiver up" is answerable from the log alone.
- An unrecognised game build now logs the running EXE's full PE fingerprint
  (TimeDateStamp, SizeOfImage and CheckSum), which is everything needed to
  author the profile for that build.
- Smoothing is now two user-configurable INI keys under `[Smoothing]`:
  `LocalSmoothing` (default `0.0`) for a tracker running on this machine
  (loopback) and `RemoteSmoothing` (default `0.15`) for a tracker on a remote
  network device. The value is selected per connection from the packet source
  address and re-evaluated every frame, so switching trackers needs no restart.
- Removed the single `Smoothing` key and the hidden 0.15 baseline floor, so
  local users get zero-latency tracking by default. Both keys cover rotation
  and position.

## [0.0.0] - 2026-06-03

### Added

- Initial scaffold from cameraunlock-core templates: x86 (Win32) CMake build,
  Ultimate ASI Loader vendoring (deployed as `winmm.dll`), OpenTrack UDP
  receiver, hotkey poller, INI configuration.
- Added camera hook in discovery mode with PE-fingerprint build profile
  routing.

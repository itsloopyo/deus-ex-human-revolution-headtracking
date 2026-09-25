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

- Head tracking stays on while you aim down sights. Rotation carries straight
  on, and the lean eases out over 150ms as the sights come up and back in over
  250ms as they come down, since a lean moves the eye off the sight line.
  There is no setting and no key for it.
- Lean collision, on by default. The eye is held off walls, doors and cover
  instead of passing through them: before the lean is added to the camera the
  mod asks the game's own collision world what stands between the eye and where
  the tracker wants it, and shortens the lean to fit. Contact is immediate and
  the release is eased, so pressing into cover stops the view dead at the
  surface and coming off it does not pop. Nothing the game reads changes - the
  question is asked about the clean camera, so aim, projectiles and the
  interaction pick are all untouched. `[Position] CollisionEnabled` turns it
  off, and `[Position] CollisionMargin` (metres, default 0.19) is how far off a
  surface to hold the eye. The standoff is held clear of the camera's near clip
  plane, which the mod reads out of the frame's own projection rather than
  assuming: this game floors that plane at 40 world units (12.7cm), and a
  standoff inside it stops the eye short of the wall while the wall itself is
  still too close to be drawn, so it opens into a polygonal cutaway. A
  configured value below the plane is raised to clear it and the log says so.
  An older file without these keys gets them, at their defaults, when the mod
  converts it to the new layout.
- 6DOF position tracking. Leaning moves the eye through the world, resolved
  against the camera's clean axes so a lean follows the body rather than the
  head-turned view, and scaled by the engine's own 316.05 units-per-metre
  constant.

### Changed

- The startup `Graphics runtime:` log line now says what it actually reports -
  which graphics DLLs are mapped a fraction of a second into the process - and
  says plainly that it is not the renderer. It read `d3d9=1 d3d11=0` on a game
  that renders through Direct3D 11 from start to finish.
- `DeusExHumanRevolutionHeadTracking.ini` has a new layout. The first time this version starts, it converts the file once into the new layout and keeps the file as it was beside it as `DeusExHumanRevolutionHeadTracking.ini.pre-canonical`. `DeusExHumanRevolutionHeadTracking.ini.pre-canonical.last`, when present, is the file as it was before the most recent conversion: the mod converts the file again when it finds the older layout later, for example after an older version of the mod rewrote it.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
  - Reticle settings, and a key that toggled the reticle.
  - The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`.
- An older version of the mod may not read the new layout correctly. It reads a key that moved as its own default, and it can misread a hotkey or another value that is now written as a name. To go back to an older version, first copy `DeusExHumanRevolutionHeadTracking.ini.pre-canonical` back over `DeusExHumanRevolutionHeadTracking.ini`, which restores the old file.
- Keys that moved or were renamed, each carried over with its value: `[General] Port` is `[Network] UdpPort`; `[General] PositionEnabled` is `[Position] PositionEnabled`, beside a new `[General] RotationEnabled`, and the two together are the tracking mode the game starts in; `[General] LeanCollision` is `[Position] CollisionEnabled`; `[General] LeanCollisionSkin` is `[Position] CollisionMargin`, still in metres; `[General] CameraDump` is `[Diagnostics] CameraDump`; `[Hotkeys] Toggle` and `ChordToggle` are `ToggleKey`, `Position` and `ChordPosition` are `CycleTrackingModeKey`, and `YawMode` and `ChordYawMode` are `YawModeKey`.
- The tracking mode (`PageUp` / `Ctrl+Shift+G`) and the yaw mode (`PageDown` / `Ctrl+Shift+H`) are now saved to the file when you change them, and the game starts in them next time. `End` still changes the current session only: whether tracking is on when the game starts is `EnableOnStartup`.
- An old file whose `Port` is outside 1024-65535 is left as it is and the mod still does not start, as before. Fix the value and the next start converts the file.
- An old file whose `DataFreshnessMs` is below 1, which kept tracking from ever running, is not converted, because the new layout takes 1 or more. The mod runs on it as before, saves nothing, and converts it once the value is 1 or more.
- When the mod cannot create its config file, for example in a folder it cannot write to, it now starts on the default settings and says so in `HeadTracking.log`. It used to stop without starting tracking.
- The aim-down-sights mode cycle retired earlier in this release no longer reads its settings: `[General] AdsMode`, `[Hotkeys] Ads` and `[Hotkeys] ChordAds` are ignored and not carried over, and `Insert` / `Ctrl+Shift+U` do nothing. Head tracking stays on through the aim (15eeb54).

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

### Removed

- `[General] ReticleProbe`, the discovery setting that swept the reticle across the screen instead of placing it on your aim. The reticle always follows the aim.
- The sensitivity, axis inversion and deadzone settings: `[Sensitivity] Yaw`, `Pitch`, `Roll`, `InvertYaw`, `InvertPitch` and `InvertRoll`, and `[Smoothing] DeadzoneDeg`. Set these in your tracker app instead. With these settings at their shipped defaults the camera moves as it did before.
- The warning about the retired `[Smoothing] Smoothing` key. The key was already ignored, and is not carried over.

## [0.0.0] - 2026-06-03

### Added

- Initial scaffold from cameraunlock-core templates: x86 (Win32) CMake build,
  Ultimate ASI Loader vendoring (deployed as `winmm.dll`), OpenTrack UDP
  receiver, hotkey poller, INI configuration.
- Added camera hook in discovery mode with PE-fingerprint build profile
  routing.

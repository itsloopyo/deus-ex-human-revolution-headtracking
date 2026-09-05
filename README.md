# Deus Ex: Human Revolution - Director's Cut Head Tracking

![Deus Ex: Human Revolution - Director's Cut running with this mod](https://raw.githubusercontent.com/itsloopyo/deus-ex-human-revolution-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Deus Ex: Human Revolution - Director's Cut that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse/controller
- **6DOF positional tracking** - lean and peek with head position

## Requirements

- Deus Ex: Human Revolution - Director's Cut (Steam, legitimately purchased)
- Windows 10/11
- [OpenTrack](https://github.com/opentrack/opentrack) or a phone head-tracking app (e.g. HeadCam)

## Installation

1. Download the latest `DeusExHumanRevolutionHeadTracking-vX.Y.Z-installer.zip` from [Releases](../../releases).
2. Extract it anywhere.
3. Run `install.cmd`. It auto-detects your Steam install; pass a path if it cannot find it:
   `install.cmd "C:\Path\To\Deus Ex Human Revolution Director's Cut"`
4. Start the game.

### Manual installation

1. Copy `plugins\DeusExHumanRevolutionHeadTracking.asi` next to `DXHRDC.exe`.
2. Copy `vendor\ultimate-asi-loader\dinput8.dll` next to `DXHRDC.exe` and rename it to `winmm.dll`.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode (world / local) | `Page Down` | `Ctrl+Shift+H` |
| Cycle ADS mode      | `Insert`    | `Ctrl+Shift+U`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

`Insert` / `Ctrl+Shift+U` cycles what happens when you aim down sights. All
three start the same way - raising the sights swings the view onto the point
the reticle was marking, so your shot lands where you had it lined up - and
they differ in what happens for the rest of the aim:

1. **Tracking paused** (default) - the game keeps the camera for as long as the
   sights are up. The sight picture is exactly the game's, and head movement
   does nothing until you lower the weapon.
2. **Tracking on, with an aim marker** - head tracking carries on from the
   snapped position, and a small white crosshair is drawn wherever your rounds
   will actually land. This white marker is authoritative, including with scoped
   weapons. A scope's built-in reticle is only accurate while your eye is
   exactly aligned with the optic, so the two reticles separate when head
   tracking moves your view off that sight line.
3. **Tracking on, no aim marker** - the same as 2 without the marker, for a
   cleaner screen when you are happy reading the sights themselves.

The choice is saved to the INI, so it survives a restart. The mod draws no text
of its own, so the mode you switched to is named in `HeadTracking.log` rather
than on screen.

## Configuration

`DeusExHumanRevolutionHeadTracking.ini` is created next to `DXHRDC.exe` on first launch. Sensitivity, smoothing, deadzone, port, and hotkeys are all configurable there.

```ini
[General]
; Yaw mode: true = horizon-locked yaw (default), false = camera-local
WorldSpaceYaw=true
; What head tracking does while the sights are up: paused, marker or tracked
AdsMode=paused
```

`AdsMode` is the setting `Insert` cycles, and pressing the key writes the new
value back here. A value that is not one of the three names falls back to
`paused`.

Smoothing is chosen per connection from the tracker's source address:

| `[Smoothing]` key | Default | Range | Applies to |
|-------------------|---------|-------|------------|
| `LocalSmoothing` | `0.0` | 0.0-1.0 | Tracker running on this machine (loopback). 0 = no smoothing, 1 = heavy |
| `RemoteSmoothing` | `0.15` | 0.0-1.0 | Tracker on a remote network device, e.g. a phone over WiFi. 0 = no smoothing, 1 = heavy |

Both cover rotation and position. Switching between a local OpenTrack instance
and a phone on WiFi picks up the other value without restarting the game.

`WorldSpaceYaw=true` (default) keeps head yaw locked to the world's up-axis no matter where the camera is pitched; `false` yaws around the camera's own up-axis instead. Toggle at runtime with `Page Down` / `Ctrl+Shift+H`.

Leaning is stopped by the level rather than passing through it:

| `[General]` key | Default | What it does |
|-----------------|---------|--------------|
| `LeanCollision` | `true` | Asks the game's own collision what stands between the eye and where the tracker wants it, and shortens the lean to fit |
| `LeanCollisionSkin` | `0.19` | How far off a blocking surface to hold the eye, in metres |

Only what you see is affected. The question is asked about the camera the game
is aiming with, so where shots go and what an interaction prompt picks are the
same whether the clamp engages or not. Contact is immediate and the release is
eased, so leaning into cover stops at the surface and coming off it does not
pop. `HeadTracking.log` names both edges.

The standoff cannot be set closer than the camera's near clip plane, which the
mod reads from the frame's own projection. Geometry nearer to the eye than that
plane is not drawn at all, so an eye stopped short of a wall but still inside it
sees straight through the wall - the cutaway the clamp exists to prevent. A
smaller value is raised to clear the plane and `HeadTracking.log` names the
number it used.

An INI written by an earlier version does not have these two keys in it. The
defaults above still apply; add the keys by hand to change them, or delete the
file and let the next launch write a fresh one.

## Troubleshooting

- **No head tracking in game:** check `HeadTracking.log` next to `DXHRDC.exe` exists after launching. If not, the ASI loader is not engaging - re-run `install.cmd`. The log is rewritten from scratch on every launch; the previous launch is kept as `HeadTracking.prev.log`, which is the one to send if the game crashed and you have relaunched since.
- **Tracking is jittery:** raise `LocalSmoothing` (tracker on this PC) or `RemoteSmoothing` (tracker on your phone or another device) in the INI (0.0-1.0).
- **View drifts:** centre it in your tracker app. The mod applies whatever pose the tracker sends, so the tracker owns the centre.
- **Yaw feels wrong when looking up or down at extreme angles:** try toggling between world-locked and camera-local yaw with `Page Down` (or `Ctrl+Shift+H`). World-locked (default) is horizon-stable; camera-local follows the camera's current up-axis.
- **Head tracking stops while aiming down sights:** that is `AdsMode=paused`, the default. Press `Insert` (or `Ctrl+Shift+U`) to cycle to a mode that keeps tracking through the aim.
- **A wall opens into a polygonal cutaway when you press into it:** the eye is inside the camera's near clip plane, where geometry stops being drawn. Raise `LeanCollisionSkin`. The mod already holds the eye clear of the plane it reads from the frame, so if this happens at the default, send the `lean-trace` line from `HeadTracking.log` - it names the near plane that frame was built with.
- **Leaning still goes through walls:** search `HeadTracking.log` for `lean-trace`. A line saying the lean is running unclamped names what the mod could not read - until that clears the clamp cannot run, and the lean is applied whole. `LeanCollision=false` in the INI has the same effect deliberately.
- **No white aim marker in the marker mode:** the marker is drawn by an overlay that attaches to the game's Direct3D 11 swap chain the first time you select the marker mode in gameplay. Check `HeadTracking.log` for `dx11_overlay: hooks enabled`; without that line the marker mode behaves like the no-marker one.

## Updating / Uninstalling

- Update: run the new version's `install.cmd` - it redeploys in place.
- Uninstall: run `uninstall.cmd`. It removes the mod and, if we installed it, the ASI loader.

## Building from source

```
git clone --recurse-submodules https://github.com/itsloopyo/deus-ex-human-revolution-headtracking.git
cd deus-ex-human-revolution-headtracking
pixi run build-release
```

Requires Visual Studio 2022 (C++ workload), CMake 3.20+, and [pixi](https://pixi.sh).

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT - see [LICENSE](LICENSE). Copyright itsloopyo / CameraUnlock.

Third-party components bundled or statically linked into the release are listed
with their full license texts in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Legal

This is an unofficial, non-commercial fan modification. It is not affiliated
with, authorised, endorsed by, or associated with Eidos-Montreal, Square Enix,
or any other rights holder in the game, nor with any of their parents,
subsidiaries or affiliates. "Deus Ex", "Eidos-Montreal" and "Square Enix" are
trademarks of their respective owners and are used here only to identify the
game this mod is for.

A legitimately purchased copy of the game is required. This mod ships no game
code, assets, decompiled source, or proprietary binaries, and does nothing to
bypass DRM or licence checks. It attaches to the retail executable at runtime;
the only game-derived data it carries are numeric addresses and struct offsets
determined by inspecting the shipped binary for interoperability.

## Credits

- **Eidos-Montreal / Square Enix** - for Deus Ex: Human Revolution - Director's Cut.
- **[OpenTrack](https://github.com/opentrack/opentrack)** - head tracking protocol and software.
- **[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)** by ThirteenAG - plugin loading.
- **[MinHook](https://github.com/TsudaKageyu/minhook)** by Tsuda Kageyu - function hooking.

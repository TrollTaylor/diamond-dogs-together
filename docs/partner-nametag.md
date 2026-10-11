# Partner name tag

A small name tag floats above the co-op partner's head. The old green ring overlay
has been removed to preserve the original game reticle. Native reticle recoloring
and teammate aim-assist filtering still require a verified native target contract;
removing the ring does not implement those features.

Status: fixture-tested (unit tests, a hidden DX11 fixture with pixel readback, the
contract generator). Not yet run in the game. The hidden-worker scenario
`tools/worker/scenarios/partner-nametag.txt` holds the runtime proof lines.

## What you see

- **Name tag**: the partner's name in white with a thin dark outline on a faint plate,
  with a small triangle pointing down at his head. It follows his stance (standing,
  crouching, prone; the height eases over 150 ms). It fades from full opacity at 40 m
  to 40% at 150 m and is hidden beyond `nametag_max_m` (300 m by default). Long names
  are cut with `...`. While the screen centre is on him, the label turns light green.
- **Hidden** during cutscenes and demos, with the iDroid open, in the pause menu,
  while loading and on the title screen, at mission end, while the partner body is
  hidden (out of range, link lost, not realized), when he is behind the camera or off
  screen, beyond the range limit, and whenever the camera check refuses.
- **Through walls**: there is no verified game raycast, so the tag also shows when a
  wall is between you.

## Names

Each PC sends its own label to the other on reliable link channel 3 (link v2 only;
the legacy lab link shows the fallback names). The first source that gives a
non-empty name wins:

1. `lab.ini` `nametag_name=` (1..24 of `A-Z a-z 0-9 space _ . -`),
2. the lab-mode `self_name=` (the lab guest's fake persona),
3. `coop.ini` `name=` (any UTF-8 up to 64 bytes),
4. the Steam persona name, asked once at startup (only when none of the above is set)
   from the fingerprinted `steam_api64.dll` (`SteamFriends015` slot 0, the same local
   query the lab loopback already makes). Normal launches never ask.

Both ends sanitize every name: strict UTF-8 decoding (a broken sequence becomes one
`?`), control, bidi, zero-width and BOM characters are removed, characters the
overlay font cannot draw (outside Latin-1) become `?`, runs of spaces collapse, and at
most 24 characters remain. Without a usable name from the partner the host shows
**Player 2** and the guest shows **Host**.

Wire format (`nametag_core.hpp`): `'N' 0x4E`, version `1`, flags `0`, length `0..48`,
the sender's link session number (u32, informational: session numbers are per
endpoint), then the UTF-8 bytes. A new link session clears the reliable channels on
both sides, and both sides send their name again. Malformed messages are dropped and
counted (`name_invalid`).

## Settings

| Where | Key | Values (default) |
|---|---|---|
| `lab.ini` | `nametag` | `0`/`1` (unset: `coop.ini`, then on) |
| `lab.ini` | `nametag_crosshair` | `aim`/`always`/`off` (unset: `coop.ini`, then `aim`) |
| `lab.ini` | `nametag_name` | 1..24 of `[A-Za-z0-9 _.-]` |
| `lab.ini` | `nametag_max_m` | `50`..`1000` (`300`) |
| `coop.ini` | `name` | this player's label, UTF-8, at most 64 bytes |
| `coop.ini` | `nametag` | `0`/`1` |
| `coop.ini` | `friendly_crosshair` | `aim`/`always`/`off` |

`lab.ini` wins over `coop.ini`. All keys are allowed in two-PC play (read-only
feature, no `play_refused` bit). The lab scripts write them only when set
(`Prepare-LabInstance.ps1 -Offline -PartnerSetting nametag_crosshair=always` or
`guest:nametag_name=Quiet`). The feature exists only in offline co-op sessions with
the partner (`offline-v1`, `partner=1`) and only when its camera contract verifies
(below).

**F9 menu, Partner page, "Name tag"**: a readout line and the buttons
**Name tag on / off** and **Name tag status**. Legacy crosshair configuration
and commands remain parseable for compatibility, but never draw a replacement
reticle or change its native color.

| Command | Result |
|---|---|
| `nametag on` / `nametag off` | the status line below |
| `nametag crosshair aim\|always\|off` | the status line below |
| `nametag status` | `nametag;on=1;xh=aim;st=visible;why=ok;name=GuestTag;src=remote;d=6.1;stance=0;px=972,322;hov=0;aim=0;stale_frames=0` |

`why` is `ok` or the hidden reason: `off`, `camera` (see the refusals),
`terminating`, `hud` (demo or iDroid), `no_partner`, `range`, `lost`, `no_body`,
`behind`, `offscreen`, `far`. `src` is `remote` or `fallback`; `d` the camera
distance in metres; `px` the last drawn anchor in backbuffer pixels; `hov` the
centre ray on the partner; `aim` the local aim state; `stale_frames` the overlay
frames that found no fresh game-thread frame. Names in this line keep only
`A-Za-z0-9_.-` (others become `_`).

## How it works

### Camera (read-only; `tools/generate_nametag_contract.py`)

The tag needs the game's own camera matrices. On the Lua owner thread, once per
partner tick, it reads (ReadProcessMemory, never a dereference):

```
sel = *(0x2ba1668)                 CameraSelector main instance (0x438a60 reads it)
vp  = *(*(sel + 0x58) + 0x18)      the render viewport (the getter 0x438db0)
vp+0x300 projection, vp+0x340 view, vp+0x380 view x projection (4x4 rows, row vectors)
vp+0x570 camera, vp+0x584 rect (x0, y0, width, height), vp+0x5d8/0x5dc pixel size
camera+0x100 flags (bit 2 = non-perspective), +0x168 near, +0x16c far
```

The game's player update (0x9c1f00, the function that also holds the 0x9c2b51 input
hook, which runs on the Lua thread) reads exactly these fields every frame without a
lock at 0x9c236c..0x9c2446, so the tag repeats the game's own pattern on the same
thread. The viewport update 0x1b9490 fills them: view = camera+0x70, projection =
0x241b00(...) and VP = View x Projection. The generator re-runs 0x241b00 instruction by
instruction in a small SSE emulator for 16:9, 21:9 and 4:3 with and without lens
shift: P00 = p*b, P11 = p*c, P20/P21 = the lens shift, P23 = 1, P33 = 0, so clip.w is
the view-space z and the camera looks along +Z (in front means w > 0.1 m). x is
mirrored (b < 0). The aspect is inside the matrix (a 16:9 clamp that widens the
horizontal view on ultrawide screens), and NDC always spans the viewport rect, so
the overlay maps `x = width*(x0 + w*(ndc.x+1)/2)`, `y = height*(y0 + h*(1-ndc.y)/2)`.

`python tools/generate_nametag_contract.py --check` pins 24 sites (the getter, the
player-update reads, the viewport update and both projection calls, the VP product,
the projection builder, the GrViewport and GrCamera constructors and their vtable
stores, the TppSubViewport override, the selector update and the aspect constants),
re-runs the emulation and requires the committed `native/nametag_contract.hpp` to be
its output. At startup the loader compares every site in the running image
(`nametag=ready` or `nametag=refused; reason=contract; rva=..`); a refusal turns the
feature off for the process.

Every tick then checks, in this order (state.json `camera.refusal`): the Lua owner
thread (`thread`), the chain (`selector`, `handle`, `viewport`), the viewport vtable
(`sub_viewport` for mirrors/photos 0x21e9e68, `vp_vtable` for anything but
0x20f1c08), the camera (`camera`, `cam_vtable` for anything but 0x20f08c8, `ortho`),
the pixel size 64..16384 (`size`), the rect (`rect`), finite values (`nonfinite`), the
projection shape (`proj_form`: |P00| and P11 between 0.05 and 1000, so the binocular
and scope zooms, which narrow the main camera to a few degrees, still pass) and
View x Proj == VP within 1e-3 per row (`torn`: the last good camera is reused for up to
two consecutive torn ticks directly after it, then the tag hides; any other refusal or
a tick without reads forgets it). A census line
`nametag_census; mission=..; refusal=..; vp_vt=..; cam_vt=..; w=..; h=..; ...` is
logged on the first tick of a mission that read the viewport's vtable (a null
selector, handle or viewport while the mission loads waits), and once more if that
one refused and a later tick passes.

While nothing can be shown (`off`, `terminating`, `hud`, or the partner body hidden)
the tick reads neither the camera nor the body nor the gun; only the names flow.

### Partner body, stance and aim

- Body: `partner_player::ReadState`, instance 1 realized with parts loaded, then the
  validated block chain `ResolveBlock(1)` gives the body transform T: T+0x10 the foot
  point (`pos - (0, T+0x160, 0)` from the verified `T.vt+0x58`), T+0x20 the position,
  T+0x60 the rotation. An implausible foot (more than 5 cm sideways or not 0.2..1.2 m
  below the position) falls back to position - 0.9 m.
- Head: foot + 1.80 / 1.25 / 0.55 m (stand / squat / prone) + 0.25 m. The stance is
  the partner's network stance (fresh within 1 s), else the body's own stance.
- Legacy hover telemetry (does not draw or recolor the reticle): the ray from the camera through NDC (0,0) (lens shift included)
  against a capsule of radius 0.30 m (stand/squat: vertical from foot+0.15 to the top
  -0.15; prone: horizontal, +-0.85 m along the body yaw at foot+0.25), 0.1..150 m.
  Local aim is the pinned gun record of the local instance (`ReadGun(k=0)`: node active
  and hold bit 0x40000), else the Lua HOLD level of the tick.

### Threads

1. **Game thread** (the observer's partner tick, after the driver): camera, body,
   aim, names, projection; then one `Frame` (about 100 bytes) is copied under a lock.
2. **Present**: takes the newest frame, maps NDC to pixels and draws on ImGui's
   background list (under the F9 menu). It never reads game memory. A frame older
   than 250 ms is not drawn (loading and the title stop the Lua tick; that the pause
   menu stops it too is expected but unverified: the scenario's `nametag-paused`
   capture decides). The DX11 backend maps one ImGui unit to one backbuffer pixel, and
   the tag maps NDC over the backbuffer size also under an open menu (whose display is
   the client rect). Hidden-worker builds draw the tag before the frame capture, so
   captures contain it.
3. **Window close** (`WM_CLOSE`/`WM_DESTROY`): a thread-safe flag hides the tag at once.

Cutscene/demo and iDroid state come from `debug_menu.lua`: STATE_V1 now has a 13th
field (bit 0 `DemoDaemon.IsDemoPlaying`, bit 1 `TppUiCommand.IsMbDvcTerminalOpened`, a
pure query registered at 0x8dad90 that reads one byte; absent or failing: 0), sent
within 5 updates of a change. The loader takes this receipt for the name tag even when
the debug menu itself is off (only STATE_V1 then; every other debug receipt stays off).

## Telemetry

Worker `state.json` (and `captures.jsonl` `roles.<role>`) gets a `"nametag"` object:
`on`, `crosshair`, `state`, `reason`, `name`, `name_src`, `local_name`, `local_src`
(`lab|self|coop|steam|none`), `name_tx`, `name_rx`, `name_invalid`, `link_session`,
`body_ok`, `body`, `foot`, `head`, `stance`, `dist_m`, `projected`, `ndc`, `w`,
`miss_yaw_deg`, `miss_pitch_deg` (+ = the partner is right of / above the crosshair),
`hover`, `aim`, `aim_src` (`gun|hold`), `crosshair_on`, `hud`, `snapshot_seq`,
`snapshot_tick_ms`, `camera` {`ok`, `refusal`, `vp_vt`, `cam_vt` (RVAs), `vp_px`,
`rect`, `vfov_deg`, `near`, `far`, `vp_err`, `torn`, `thread`} and `render` {`frames`,
`drawn`, `stale_frames`, `hidden_frames`, `last_px`, `label_box`, `crosshair`,
`crosshair_frames`, `ring`, `bb`, `display`, `alpha`, `present_thread`, `age_ms`}.

`script-observer.log`: `nametag=ready; ...` / `nametag=refused; reason=..; rva=..`,
`nametag_census; ...` (at most twice per mission, above), `nametag=visible; ...` / `nametag=hidden;
reason=..; refusal=..` (on each change, at most 256 lines per process),
`nametag=name_tx; ...`, `nametag=name_rx; ...`, `nametag=name_rx_refused; ...`,
`nametag=setting; ...` and `debug_menu=nametag; id=..; ok=..; result=..`.
`debug-overlay.log`: `overlay=nametag_drawn; frame=1` (first closed-menu tag frame).

## Hidden-worker proof

`tools/worker/scenarios/partner-nametag.txt` (the header lists the evidence): the
scenario names the two copies `HostTag` and `GuestTag` (`labsetting host|guest
nametag_name=..`), places the host 6 m behind the guest, expects the status line,
captures idle, aiming (a small mouse sweep; tune it from the first run's
`miss_yaw_deg`), prone, turned away, off, the menu and the pause screen, then places
the guest behind the host and expects `name=HostTag`.

## Files

`native/nametag_core.{hpp,cpp}` (pure: projection, camera checks, capsule, easing,
fade, sanitizer, wire, commands, status, JSON), `native/nametag.{hpp,cpp}` (settings,
the frame handoff, render counters, telemetry), `native/nametag_game.{hpp,cpp}` (the
game-thread source), `native/nametag_contract.hpp` (generated), the overlay layer in
`native/debug_overlay.cpp`, `tools/generate_nametag_contract.py`. Tests:
`debug_menu_tests` (core, STATE_V1, handoff), `worker_debug_tests` (JSON),
`lab_config_tests` (keys), `debug_overlay_tests` (DX11 readback),
`debug_menu_lua_check` (HUD bits).

## Limits and risks

- The main viewport is built by protected code (0x3b759ba calls the GrViewport
  constructor); that it keeps vtable 0x20f1c08 is a static inference. A different
  runtime vtable refuses (`vp_vtable`) and the census line names it; pin it then.
- Whether the Lua tick runs before or after the camera update in a frame is not
  known: the tag may trail a fast camera turn by one frame.
- No occlusion test (tags show through walls) and no off-screen arrow.
- Names outside Latin-1 (for example Japanese or Cyrillic Steam names) show as `?`
  (the overlay font has no such glyphs).
- The iDroid binding is `TppUiCommand.IsMbDvcTerminalOpened` (registration table and
  the query body checked statically; the table name is inferred from neighbouring
  `TppUiCommand` entries).
- The pause menu is covered only by the stale-frame rule (the Lua tick is expected to
  stop there; the `TppUiCommand` table has no pause query). Screens during which the
  Lua tick keeps running without a demo or the iDroid (the game-over screen, script
  fades to black) still show the tag.

## Native targeting investigation, October 9

The extra green ring and its F9 controls have been removed. The original game
reticle is preserved; native green recoloring and teammate aim-assist exclusion
are not yet implemented. Legacy config/commands remain parseable, with drawing
disabled. The geometry hover telemetry is not an occlusion-tested game target.

`tools/generate_native_ui_contract.py --check` verifies the exact retail EXE SHA
and 12 native lifecycle, real ray-hit and UI producer sites. This establishes:
Player secondary P+D8 owns final UI secondary U (vtable 23AF700), U+28 is P;
reticle update FEA420 reads the native camera handle, stores its ID at U+24,
classifies it at FE7270, and forwards category 0/1/2 to HudSystem vt+140. The
classification meanings/colors still require observed evidence.

Read-only `native_ui_census` logs actual hit/ray IDs, cached category and HUD
setter/provider vtables. `partner_hud_job_census` compares UI flags and action
cache around only the already validated remote deferred job, with the remote
Q+20 action byte. Native producer 124A5C0 calls UI vt+60 and sets shared HUD+328
bit3 for action 0x12; whether this is Quiet sprint remains unverified. No census
calls game functions, changes UI memory or patches these native targets.

### Native reticle diagnostics, 2026-10-09

Private paired run `6eef887cb2e04cd785bee9eb6f968119` captured native UI hit ID 1, category 1 and the original red WU pistol reticle. The exact retail executable has been traced from the UI classifier to the game's two existing reticle nodes and their RGB setters. Category 2 is the game's friendly classification, including hostages, D-Horse and D-Dog; its actual RGB is now measured through `native_reticle_rgb_census`. No alternate crosshair geometry is drawn.

The classifier defaults to the scoped teammate filter. It changes only category 1 to native friendly category 2 for ID 1 in the current qualified offline two-player UI. It verifies the sole native caller, both final UI vtables, current player ownership, the current player's UI pointer and roster IDs 0/1. Soldiers, local targets, unknown categories, other UI owners and native online sessions preserve the original result. Reticle coloring and camera aim filtering have independent modes; enabling one does not enable the other.

The name-tag installation must fingerprint the original native UI bytes before teammate targeting installs its classifier hook. `SetReticleMode` and `ReticleStatusLine` support worker-only A/B testing. On the Lua owner thread, `ResetNativeUiCensus` restarts the capped per-mission observations. These are private-worker A/B controls. Production defaults enable the qualified aim, reticle and Quiet filters after their exact code and owned-pair gates pass.

The remote-job census now includes the exact shared HUD provider's selected action and flag word before/after the validated partner job. It reads only. The scoped Quiet hint producer is filtered after its natural partner-job ownership was captured. The current build also reports ray-only hit changes that an earlier census could suppress.

The Quiet investigation also identified hint 0x78/priority 2 queued by the movement code only for camo 0x74. The native queue lives at UI secondary +370 (ID), +374 (flags, enable is bit 9), +3A0 (pending) and +3A3 (priority). These four fields are now observed around the partner job and in the periodic census. This path is independent of the unchanged action cache; private run 65c7971abb4643e5a25e619cfc36e7da captured 48 exact remote hint calls and suppressed all 48, with no refusals. That run did not visibly reproduce the prompt, so the evidence qualifies the scoped producer rather than a screenshot comparison.

Quiet dash HUD isolation (qualified filter by default): the exact native
Quiet-camo producer calls hint queue `FE8690` with ID `0x78`, priority 2 and
return address `10A498E`. The existing validated instance-1 job callbacks arm a
thread-local scope; it ends before after-job reads, independently of log caps.
An explicit filter suppresses only that helper call on the owner thread with
the current player/UI chain, offline two-player flags and roster 0/1. It skips
the original queue entirely, preserving its four hint fields; host Quiet calls
outside this scope and every other hint retain all original arguments. Full
phase1, state callbacks, constructor, selector and event-table bytes are pinned
in `native_ui_contract.hpp`. Live pre/post queue and visible prompt comparison
remain required before enabling this by default. Native reticle and aim modes
are independent from the Quiet hint mode.

Private run `9c7ab0d615f84558b4facccdd1877ecf` used real in-process controller aim input against a teammate: observation matched 58 candidates, and filtering removed all 57 candidates in its independent phase, with zero refusals. Run `65c7971abb4643e5a25e619cfc36e7da` captured the actual native WU pistol reticle in green with its original geometry. These hidden local tests do not establish a fresh two-PC session.

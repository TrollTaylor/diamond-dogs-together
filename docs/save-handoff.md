# Automatic save handoff (join-fix phase 2)

When a guest joins with a co-op save that cannot meet the host (a different
title-helicopter location, a save still in the story opening, or one bound to
a mission co-op cannot join), the host's progress files are copied to the
guest over the link, verified and staged at the title screen; MGSV then
closes and Play-Coop applies the staged set after the process exited and
starts MGSV again once. The guest's own co-op save is moved aside first and
comes back with Share-Save.bat [R]. A travel lock keeps an unarmed, linked
guest from wandering away from the host meanwhile
([world-sync-protocol.md](world-sync-protocol.md) section 13).

Parts:

| Part | Where | What |
|---|---|---|
| Wire codec, slot table, set hash | `native/save_handoff_wire.*` (mgs_coop_core) | P2-4..P2-6 below |
| Host/guest state machine | `native/save_handoff_core.*` (mgs_coop_core) | P2-7..P2-10; pure, a caller clock, an Actions ring |
| Shared UI state | `native/save_handoff_state.*` (mgs_coop_core) | the F9 Shared save section, worker state.json, CFG r[12..13], link-status |
| Module glue | `native/save_handoff.cpp` (mgs_coop_lab) | one Core on the link thread, the file actions, the exit request, the SAVE receipt |
| Transport | `native/partner_net.cpp` | reliable channel 4, the handoff hooks, `ReliableQueued`, `BuildGuestJoinCode`, the link-status keys |
| Lua | `lua/coop/world_handoff.lua` | the save report, the travel lock, warnings, the replay hold |
| Tooling | `tools/friend/FriendCommon.psm1`, `Play-Coop.ps1`, `Share-Save.ps1` | apply after exit, relaunch, cleanup guard, rejoin, restore |

## Why a restart (fact checks)

- The game reads the save before the link connects: world=configured and the
  install come about 0.5 s before the link (C-user-guest.log:22, :31, :57),
  and mission 1 already ends pointing at the title helicopter 40050 (:75).
- After the read an in-process swap is overwritten from memory by
  SaveGameData (package-02.lua:431, :902, :983, :2167), and ReturnToTitle
  aborts into the helicopter with isNoSave (package-02.lua:635-640) without
  reloading. So v1 always restarts, and the files are applied by tooling
  after the process exited (nothing the game writes on exit can mix in).
- Play-Coop's watchdog polls every 2 s and cleans up on the first exit it saw
  (Play-Coop.ps1:131-153); Invoke-CoopPlayCleanup runs whenever the session is
  active. The relaunch therefore needs a claim under the play mutex before
  any cleanup and a cleanup guard while a live relaunch claim exists.
- The first WM_CLOSE reaches PartnerShutdown only when the overlay subclassed
  the window; in the worker a WM_CLOSE exit was graceful in some runs and
  forced in others, so a slow exit is reported (exit_slow, toast G9).
- Every bound in partner_net loops on `link::kReliableChannels`; a peer built
  before channel 4 drops channel >= 4 silently and never acknowledges it, so
  the receiver pulls (WANT) and every channel-4 byte is gated on the peer's
  build.
- A waiting Steam guest never reads link-secret.txt; only the host builds
  codes. The guest therefore keeps the code it was armed with in memory
  (`partner_net::BuildGuestJoinCode`) and writes `link-rejoin.txt` only at the
  exit request, so it can reconnect after the restart without a new code.

## Wire (P2-4, P2-5)

Reliable channel 4 (`link::kReliableChannels` = 5; 0 link, 1 world, 2..3 game,
4 save handoff). Only `save_handoff.cpp` sends on channel 4 or drains it, from
the handoff hooks on the link's receive/transport thread. The ascending pack
order keeps channels 1..3 ahead; the glue keeps at most 4 new CHUNKs per tick
while `ReliableQueued(4)` < 16. Nothing but STATE goes out until the guest
sends WANT, and no channel-4 byte at all unless `LinkInfo.remote_build` passes
`RemoteBuildAtLeast(0.13.0)`.

Header (8 bytes, little-endian): u8 magic 0x53, u8 version 1, u8 kind,
u8 flags 0, u32 xfer (nonzero per offer, BCryptGenRandom; 0 only for STATE).

| Kind | Dir | Size | Body |
|---|---|---|---|
| 1 STATE | both (`latest`) | 64 | role, phase (0 unknown, 1 loading, 2 title, 3 ingame), story, mode, mission u32, bound_mission u32, set_known, need (guest), elig (host), 0, set_sha256[32], pad[8] |
| 2 OFFER | host -> guest | 52 + 40n (n 1..6) | n, 0, u16 0, total u32 (<= 8 MiB), bound_mission u32, set_sha256[32], n x {slot, 0[3], size u32 1..4 MiB, sha256[32]} ascending |
| 3 WANT | guest -> host | 12 | slot mask |
| 4 DECLINE | guest -> host | 12 | reason 1 same, 2 same_scene, 3 off, 4 budget, 5 user, 6 not_title, 7 bad_offer, 8 disk_space |
| 5 CHUNK | host -> guest | 16 + len (<= 1016) | slot, 0, u16 len 1..1000, u32 offset, payload (offered slot order, offsets contiguous) |
| 6 END | host -> guest | 12 | total u32 |
| 7 RESULT | guest -> host | 12 | status 0 staged, 1 exiting, 2 failed; reason |
| 8 ABORT | either | 12 | 1 link_lost, 2 user, 3 stall, 4 snapshot_changed, 5 not_title, 6 shutdown, 7 protocol |

Decoding is strict (exact sizes, ranges, zero pads); a violation is dropped,
counted and logged `save_handoff=bad_msg; why=<reason>` at most once per 10 s.
There is no resume: a new link session restarts the transfer, and a verified
staged set is reused.

## Files and the set hash (P2-6)

| Slot | Entry | Folder |
|---|---|---|
| 0 | 287700/local/TPP_GAME_DATA0 | `<save_dir>\287700\local` |
| 1 | 287700/local/TPP_GAME_DATA1 | |
| 2 | 287700/local/PERSONAL_DATA0 | |
| 3 | 287700/local/PERSONAL_DATA1 | |
| 4 | remote/TPP_GAME_DATA | `<save_dir>\remote` (the save redirect's RemoteStorage folder) |
| 5 | remote/PERSONAL_DATA | |

Slot 0 or 1 must be present for a set the host offers (its snapshot, the
OFFER decode and the guest's manifest check). The guest's own set may also be
identified by slot 4 alone (`remote\TPP_GAME_DATA`, with or without
`PERSONAL_DATA`): a co-op copy seeded from an old Steam save has only the
remote files until the game first writes a local slot, and without a set the
guest never decided its need (online 2026-10-22 P1: STATE `need=0
set=00000000`, the travel lock fell back to its story-start escape and the
friend's second press loaded its own story mission alone). Any other name matching
`^(TPP_GAME_DATA[0-9]?|PERSONAL_DATA[0-9]?)$` in either folder makes the set
`unexpected_files`. `set_sha256` = SHA-256 over the present slots in ascending
order, each contributing u8 slot, u32 size LE and its raw 32-byte SHA-256; hex
is upper case like Get-FileHash. Settings files (TPP_CONFIG_DATA*,
TPP_GRAPHICS_CONFIG, MGO_GAME_DATA*) are never hashed, sent or replaced.

The glue hashes one file per link tick and only while the last Lua report is
fresh (<= 3 s) with saving=0; local slots are streamed with
FILE_SHARE_READ|WRITE|DELETE, remote slots are read whole through
`lab::save::read_remote_snapshot`, which takes the redirect's I/O lock shared
while RS_FileWrite / RS_FileDelete take it exclusive: a game write that arrives
during the read waits for it (it never fails on a sharing violation) and the
read never sees half a file. A file is read again only when its size or
last-write time changed. An empty slot file gives an invalid set; a file over
4 MiB is not read and makes the total exceed 8 MiB (too_large). The glue
logs its own set once per change: `save_handoff=set; ok=1; set=<8 hex>;
files=N; local=0|1` or `save_handoff=set; ok=0; why=read|no_primary|hash;
files=N` (an unstable pass logs nothing; the next one decides). The host
snapshot reads the set into one VirtualAlloc block (at most 8 MiB), then
hashes every file again at least 500 ms later; any difference refuses it. The
memory is freed on release, failure and shutdown.

## Decisions (P2-7, P2-8)

Host eligibility (`elig=1` only when all hold; otherwise the first failing
reason in this order):

| Reason | Condition |
|---|---|
| off | `save_handoff=off` |
| story_unknown | no fresh Lua report, or the story constants unreadable or unordered |
| story_opening | `str_storySequence < CLEARD_TO_MATHER_BASE` |
| unexpected_files | another progress name in the save folders |
| too_large | total > 8 MiB (or a file > 4 MiB) |
| saving | `TppScriptVars.IsSavingOrLoading()` |
| continue_unknown | the current set has no provenance entry |
| continue_unsupported | the set continues into a mission outside 30010/30020/30050/40010/40020/40050 |

Guest need (`GuestNeed`): 0 when the policy is off or the sets are equal.
`always` gives 1. `scene` (and `stage`) give 1 when the guest's story is not
open, its own set is bound to an unsupported mission, or the location of its
title (ACC) mission differs from the host's bound mission (or the host's
mission when unbound); locations are world_follow's SUPPORTED map (10, 20,
50). Otherwise 0 (`same_scene`).

The host offers only when the guest's STATE says title, mode >= 1, need 1 and
a different set; the host is eligible; the peer build passed; and fewer than 3
offers went out for this set in this link session.

Provenance binds a set to the mission it continues into: (a) a Lua CONTINUE
(k==4) into M, after a double read 500 ms apart (source `continue`); (b) 10 s
in game in 30010/30020/30050 with saving=0, fresh reports and an unchanged set
(source `settle`; `kSettleBindMs`, 30 s until 0.13.5). The window was never
the guard against a set saved for the next mission (SaveGameData(next) right
before a load): every mission load reports loading at once (world_handoff
`X.leaving` in `e.ReservePlayerLoadingPosition`), which ends the window before
the vanilla load, and a report gap over 3 s, saving=1 or any set change
restarts it. In the 2026-10-22 session a guest waited 16 s at its title for
the host's 30 s in Mother Base after a deploy (a deploy is no Continue bind).
Up to 8 entries (LRU) in `<save_dir>-handoff\provenance.json`
(written with WriteFileAtomic, read at Configure, rewritten when malformed):

```
{"format":"mgs-coop-save-provenance","v":1,"entries":[{"set":"<64 HEX>","mission":30010,"source":"continue|settle","utc":"...Z"}]}
```

### Messages while the guest waits (online 2026-10-22 P3, P6)

| Situation | Guest | Host |
|---|---|---|
| Host's set not bound, host in game in 30010/30020/30050 (the settle bind runs) | G11 "The host is in <M>: their save is checked there (about 10 s), then copied to you at the title screen and MGSV restarts once." (the seconds only for a host build at least 0.13.6, `kSettle10Min*`: a 0.13.5 host still takes 30 s, so an older host gets the sentence without them); F9 `wait_host` why 2 "The host's save is checked in their mission, then copied to you at the title screen." | H8 "Checking your save here (about 10 s; stay in this mission), then it is copied to <partner>."; F9 refused why 4: "Your save is shared once it is known to continue into Mother Base or free roam: Continue there, or stay there a few seconds." |
| Host's set not bound, host elsewhere | G4 (now "...copied to you at the title screen...") | H3 |
| Host eligible, guest in game in a mission (the OFFER needs its title) | G12 once per link session "The host's save is ready for you: go back to the title screen (or quit MGSV and start Play-Coop again) to get it. MGSV then restarts once on it."; F9 `wait_host` why 1 | (idle) |
| Nothing decided yet (`idle`) | F9 "Comparing your save with the host's; nothing to share yet." (it shared the `same` sentence) | the same for the host |

`View.why` for `wait_host` is `save_handoff_state::WaitWhy` (0 host_elsewhere,
1 title, 2 settling). G11 / H8 replace G4 / H3 per state: a host that leaves
Mother Base for its helicopter before the bind still gets H3 (and the guest
G4), each once per state and link session. The Lua title refusal says the
same: NOTE 7 b=4 "The host is in <M>; their save is checked there and then
copied to you." when the host is in game in a co-op scene (b=3 otherwise;
no duration, since the window is the host build's).

The guest's start-up mission 1 (the Lua report says ingame=1, title=0 there)
is no location and no "in game" (review of the online 2026-10-22 fixes): the
location rule (`Loc(1)` = 0 against the host's) decides nothing there, the
last decision stands (none at a boot: `idle`) until a real mission, normally
the title; the story and set rules still decide at once. G12 / why 1 need a
mission of 10000 or more. Before, a guest whose title is at the host's place
got need=1 why=location while booting, G11 / H8 for that boot, and with the
host eligible the once-per-session G12 ("go back to the title screen").

## Staging (P2-9)

`<save_dir>-handoff` (`save_redirect::format_handoff_dir`; refused when the
folder plus the tooling's longest staged name,
`\<xfer8>.partial\287700\local\TPP_GAME_DATA0.mgs-coop-<32 hex>.tmp` (91
characters), would reach 248 characters). In play it is
`<game>\mgs-coop\coop-saves-handoff`, in the worker `run\<role>-saves-handoff`.
Order of writes on the guest:

1. `<XFER8>.partial\{287700\local,remote}\<name>`: each CHUNK written at its
   offset (offsets arrive in order; offset 0 truncates).
2. `handoff.json` inside it (WriteFileAtomic):
   `{"format":"mgs-coop-save-handoff","v":1,"xfer":"0A1B2C3D","created_utc":"...Z","host_build":"<link remote_build>","host_mission":30010,"set_sha256":"<64 HEX>","files":[{"entry":"287700/local/TPP_GAME_DATA0","bytes":199916,"sha256":"<64 HEX>"}]}`
3. The commit: every staged file is read again and compared with its manifest
   entry (size and SHA-256, nothing unlisted, the set hash recomputed), then
   `MoveFileExW` renames `<XFER8>.partial` to `<XFER8>` (never into an
   existing folder).
4. `request.json`, last and only in scene or always mode, only for a committed
   stage with its handoff.json:
   `{"format":"mgs-coop-save-handoff-request","v":1,"pid":N,"process_start_utc":"...Z","xfer":"0A1B2C3D","set_sha256":"<64 HEX>","mode":"scene|always","rejoin":0|1,"created_utc":"...Z"}`

The tooling writes `applied.json` there after applying:
`{"format":"mgs-coop-save-handoff-applied","v":1,"set_sha256":"..","applied_utc":"..","aside":"<leaf>"}`.

Native deletes only known names (the slot files and their `.tmp`,
handoff.json and its `.tmp`, request.json) and empty folders, never
recursively, and request.json first. At Configure the glue removes every
`<8 hex>.partial` folder this way and a request.json whose pid is not this
process (or that does not parse); a foreign file keeps its folder. The guest
reads applied.json for toast G10 when it was written at most 30 minutes before
(or 5 minutes after) this process started. Play-Coop starts the guest again
with `save_handoff=off` (one handoff per session), so a guest disabled by
`off` still reads it: it logs `save_handoff=applied_seen`, posts G10 and
publishes the `applied` token.

## States

The link-status / F9 token (`save_handoff_state::Token`, P2-13 order):
`off, idle, same, wait_host, offered, sending, partner_staged,
partner_restarting, receiving, staged, countdown, exiting, kept, refused,
failed, applied`. CFG r[12] (Lua): 0 none/off/host, 1 wait_host, 2 receiving,
3 staged or countdown, 4 exiting, 5 kept, 6 failed or refused. r[13]: the
travel lock bit (the effective `lab.ini` `travel_lock`, the F9 unlock not set,
guest role).

## Title Continue (online 2026-10-22 P2)

The first refused title Continue reopens the menu, and players press again: in
the session the second press 2.1 s later played the guest's own save alone
(and with a handoff pending it would have declined it for the whole process).
Since then (`lua/coop/world_handoff.lua` `again()` and `X.decline`):

- At the title, presses under 3 s apart are one double press: never an escape
  or a decline (no new note either). The deliberate repeat is 3 to 10 s after
  the refusal (NOTE 7 b=1/b=2 say so).
- A pending handoff (CFG r[12] 1..4: NOTE 7 b=3/b=4 and NOTE 9) has no
  Continue escape for its first 60 s at this title, counted from the first
  tick or press that saw the pending code (`X.pendingAt`, reset when the code
  leaves 1..4 or the mission changes). A press in the repeat window logs
  `decision=held` and posts the note again. F9 > Partner / Co-op > Shared save
  > Keep my own save is the explicit way out at any time; after the 60 s a
  repeat 3-10 s apart still declines (SAVE op 3, NOTE 10) and plays the own
  save.
- Code 1 (wait_host) at the title is a travel-lock rule: with the lock on it
  is refused (NOTE 7 b=3/b=4) before the peer rules, so also while the host
  loads (CFG peer mission 0, as at 17:46:14 in the session), is in a story
  mission, is not linked or a host transition is queued (the replay hold keeps
  it). Before the review fix those returned allow ahead of the title rule, and
  the first press loaded the guest's own save. With the lock off (F9 Travel
  lock unchecked) code 1 is allowed at once; only codes 2..4 (NOTE 9) are
  refused without the lock.
- Away from the title (in-mission Continue, travel) nothing changed.

## Exit (P2-10)

PostExit happens only when the mode is scene or always; the stage is
committed, the request written and RESULT staged sent; the last Lua report is
at most 1.5 s old with title=1, saving=0 and ingame=0; no partner mission is
armed and the game is not terminating (the glue passes the runtime mission
from the observer and counts a title 4xxxx or start-up mission as none); the
10 s countdown ran with the title held throughout; and no decline arrived (F9
'Keep my own save', or a Lua DECLINE: Continue pressed twice after the first
60 s, see "Title Continue"). Then RESULT exiting goes out and, once channel 4
is empty (`ReliableQueued(4)` 0: RESULT exiting acknowledged), it was refused
unsent, or at most `kExitFlushMs` (1 s) later, the largest visible, unowned
top-level window of this process gets `WM_CLOSE` (EnumWindows; debug_overlay
is not involved): `save_handoff=exit_flush; ms=N; acked=0|1`. Before online
2026-10-22 P4 the WM_CLOSE went out in the same drain; the overlay's
PartnerShutdown parked the Steam link before the RESULT left, so the host
never got H2. The host now also infers the restart: a scene/always guest
(its last STATE mode) with a verified stage (`partner_staged`, host phase
done) whose link closes is restarting on the host's save (its countdown exit,
a manual quit or a crash; the tooling applies the request on any exit):
`partner_restarting`, H2 and `save_handoff=partner_restarting;
how=link_closed`. A stage-mode guest never exits, so its leaving stays
`partner_staged`. Without
a window: `save_handoff=exit; how=no_window` and toast G9; still alive 20 s
later: `save_handoff=exit_slow` and G9. After a WM_CLOSE the overlay's
PartnerShutdown stops the Core (and a Steam link parks), so the glue arms its
own one-shot timer-queue timer for that line and G9; the Core logs exit_slow
only for no_window (G9 was shown at once). Losing the title for more than 2 s
in the countdown, or a decline, sends ABORT, deletes request.json then the
stage, and sets the state failed (not_title) or kept (decline). While exiting
(the game still runs: no_window or a slow close) a decline (Lua op 3, F9) still
does that (kept), and so does the guest's own Continue (Lua op 2: failed
not_title); a stale report never cancels an exit, since reports stop during a
legitimate slow teardown. A steam-wait guest writes
`<game>\mgs-coop\link-rejoin.txt` (one MGS2 code and CRLF) right before the
exit; it is never collected and Play-Coop turns it into link-secret.txt plus
`net_peer_steam` under its mutex.

## lab.ini (P2-11)

| Key | Values | Default |
|---|---|---|
| `save_handoff` | `off`, `stage`, `scene`, `always` | absent = off (Play-Coop writes it) |
| `travel_lock` | `0`, `1` | absent = 1 in play, 0 in lab (`EffectiveTravelLock`) |

Neither is a play refusal. `stage` transfers and stages but never writes
request.json and never exits (worker check R2). The transfer configures only
with `net=1`, link v2, redirected saves, world >= 1 and a handoff folder that
fits; otherwise `save_handoff=disabled; reason=off|legacy_link|saves|world_off|path`.
The travel lock bit works with `save_handoff=off`. A disabled guest whose
lock may apply (world on, effective `travel_lock`) still publishes an
available F9 view with the `off` token, so the Shared save section shows the
off line and the Travel lock checkbox. NOTE 9 carries a=0 from Lua (CFG r[12]
has no percent); `world_link::HandleNote` fills in the native transfer
percent.

## Native glue (save_handoff.cpp)

- Configure (observer install, after ConfigureWorld and before
  partner_net::Start): records the role and the travel lock, checks the gates,
  resets the Core (pid, process start, BCryptGenRandom transfer ids), removes
  leftovers, loads provenance.json and, for the guest, a fresh applied.json;
  `partner_net::SetHandoffHooks` only when configured.
- `HandleLua` (SAVE receipt, runtime thread, under the receipt lock): parses
  only; the report goes into a seqlock, CONTINUE and DECLINE into an 8-slot
  mailbox; at most 4 receipts per second (a DECLINE always passes).
- Hooks (receive/transport thread, no partner_net lock held): `on_idle` feeds
  the peer build gate, the Lua report and mailbox, the F9 Keep my own save
  request, the observer's mission notes, up to 32 channel-4 messages, the hash
  and snapshot jobs and Tick, then runs the Core's actions in order (a
  refused CHUNK makes the rest of that drain's CHUNKs refused so the core
  rewinds); `on_session` and `on_peer_close` feed the Core directly.
- `NoteMission` (observer: the runtime entry and the returned termination),
  `LuaCode`, `LockAllowed` and `GuestHandoffActive` use atomics only.
- `Shutdown` (PartnerShutdown, before partner_net::Shutdown) sets a flag; the
  next hook tick runs Core::OnShutdown (ABORT shutdown when mid-transfer) and
  frees the snapshot.

## Log lines (script-observer.log, `; t_ms=N` appended)

Glue: `save_handoff=configured; role=host|guest; mode=<mode>; channel=4; dir=ok`,
`save_handoff=disabled; reason=<reason>`, `save_handoff=cleanup; removed=N`,
`save_handoff=io_error; op=<action>; error=<win32>` (at most once per 10 s),
`save_handoff=rejoin; written=0`, `save_handoff=exit_slow` (the timer after a
WM_CLOSE), `save_handoff=set; ok=..` (the own set, once per change). Core
(P2-16): `state_rx`, `need`, `provenance`, `offer`, `decide`, `progress`,
`staged`, `countdown`, `exit_flush`, `exit`, `exit_slow` (no_window), `abort`,
`failed`, `result_rx`, `partner_restarting; how=link_closed`, `applied_seen`,
and the single
`save_handoff=bad_msg; why=<short|magic|version|kind|size|bounds|role|direction>; count=N`
(at most once per 10 s; role and direction extend P2-5's decode reasons).
The HUD (online 2026-10-22 P8) logs every world note and save handoff toast it
is given: `hud=toast; kind=world_note|save_handoff; code=N; a=..; b=..[;
partner=..; local=..]; on=0|1[; skipped=N]`, one (kind, code) at most once per
10 s (`coop_hud::SetEventLog`, set by partner_net::Start).

## link-status.json, state.json and the HUD

`"handoff":"<token>","handoff_pct":<0-100>,` after the world keys and before
`time_utc` ([link-protocol.md](link-protocol.md) 7.2; 1536-byte buffers). The
worker state.json adds `"handoff":"<token>","handoff_pct":N,"travel_unlocked":0|1,`
after `world_pair_wait`. The HUD LinkView carries `handoff` and `handoff_pct`.

## Tooling contract (P2-17, P2-18)

- Play-Coop records `game_pid` and `game_start_utc` when it sees the game and
  writes `save_handoff=scene` (or `-SaveHandoff off|always`) and
  `travel_lock=0` for `-TravelLock off` into lab.ini.
- After the game exits, `Invoke-CoopHandoffAfterExit` (console or watchdog)
  claims the request under the play mutex (state `applying`), checks it
  (format, pid and start of the exited game, under 30 min old, no reparse
  point, the staged files against handoff.json, the recomputed set),
  imports it with `Import-CoopProgressFiles -Suffix '-before-handoff'` (the
  old co-op copy goes to `mgs-coop\coop-saves-replaced\<stamp>-before-handoff`,
  the K18 marker gets `source=handoff`), writes applied.json, rewrites
  lab.ini with `save_handoff=off`, converts link-rejoin.txt for a steam-wait
  guest, records every new file hash, sets `relaunching` (count 1) and starts
  MGSV through Steam. One restart per session; any failure leaves the co-op
  save unchanged.
- The relaunch (online 2026-10-22 P5: Steam ignored the request and MGSV came
  back 3 minutes later): `Wait-CoopSteamAppStopped` waits up to 30 s until
  Steam no longer marks MGSV as running (`Get-CoopSteamAppRunning`, read only:
  `HKCU:\Software\Valve\Steam\Apps\287700` `Running` or `HKCU:\Software\Valve\Steam`
  `RunningAppID` = 287700), then 2 s (5 s when the flag cannot be read) before
  `steam://rungameid/287700`. The Play-Coop window asks Steam again every 20 s
  while no game runs and Steam does not mark it running, and prints the
  manual line ("MGSV is not back yet: start MGS V from Steam yourself (press
  Play); co-op stays on for N more seconds.") at 45 s instead of 120 s; the
  helper retries the same way once the
  window is gone. The manual line needs the window's own relaunch check: a
  retry that passed or, when Steam still marked MGSV as running so no retry
  went out, `Start-CoopRelaunch -CheckOnly` at 45 s (the same checks, no
  request; `relaunch=checked`); with Steam's flag still set it reads "MGSV is
  not back yet, and Steam still shows MGS V as running: once Steam shows Play
  again, press it; co-op stays on for N more seconds." (review fix: before, a
  stuck flag gave no line at all until the 300 s limit). The window prints
  the applied save and "Waiting for Steam to let MGSV start again (up to
  30 s)..." before that Steam wait (`Invoke-CoopHandoffAfterExit -OnApplied`),
  not after it. Every step goes to `mgs-coop\play-coop.log` (`Write-CoopPlayLog`:
  launch, game seen/exited, claim, apply, Steam's flag, each request or retry,
  the manual hint; states, times and pids only, never a code or a SteamID;
  over 1 MB it moves to `play-coop.old.log`), and Collect-Logs collects it.
  `Get-CoopRelaunchWait` reads `play-session.json` again for up to 1 s before
  it reports co-op as switched off: a read that races the helper's File.Replace
  is no switch-off.
- `Invoke-CoopPlayCleanup` does nothing while `handoff.state` is `applying` or
  `relaunching`, the claim is under 300 s old and its process lives (`-Force`
  bypasses it).

## Restore

`Share-Save.bat` [R] restores the newest copy that an import or an automatic
handoff replaced (`<stamp>-before-import` or `<stamp>-before-handoff`) and
names which one it is. Install-Coop's reseed guard recognises a handoff copy
and asks twice before replacing it.

## Checks

Unit: `save_handoff_wire_tests`, `save_handoff_core_tests`,
`save_handoff_io_tests` (the file actions on fake save trees, the exit window
choice, the install gates), `link_protocol_tests` (channel 4 packs last),
`link_udp_tests` (608,012 bytes over channel 4, the session reset, the hooks,
the status keys and the 1536-byte worst case), `world_receipt_tests`,
`lab_config_tests`, `steam_shim_patches_tests` (the handoff paths, the
remote snapshot under concurrent writes), `worker_debug_tests`, and the Lua
checks. Hidden worker runs R2..R7 in the phase-2 spec (handoff-stage,
handoff-restart, handoff-after-restart, travel-lock and the regressions).
Online 2026-10-22: `save_handoff_core_tests` TestRemoteOnlyGuest,
TestSettleWait, TestGuestInGame, TestExitFlush, TestHostInfersRestart;
`save_handoff_io_tests` (the remote-only guest set and its log line);
`steam_transport_tests` (the texts and the toast log); the Lua checks (the
title double press and the 60 s hold, NOTE 7 b=4, NOTE 3 b=5, no NOTE 6 into
the partner's mission); `friend_package_tests` (Steam's flag, the wait,
play-coop.log, Collect-Logs); `worker_session_tests` (`-GuestSaveSeed
real-remote`). Worker scenario `handoff-double-press.txt` (with `-GuestSaveSeed
real` or `real-remote`).

# World sync and field partner tests (2026-10-05–06)

## October 6: integrated title Continue, travel and alignment

The current **0.11.1** DLL is
`4666A7DA7ED2711CED7D397ED0EFF92E41782C27127DFF9374C63B1DBBF99D65`
from `046bb63`, installed and hash-verified in both copies. Run
`build/lab/runs/20261006-115055` verifies the startup fix: only the host uses
native title Continue; the guest receives all 47 records and starts on foot
at the host's same position `(1232994,20775,-125517)` mm, with player IDs
`0,1` and parts states `3,3`. Both pair in Africa `30020`, epoch `3`, roster
`2220786415`, 265 soldiers and 68 CPs. Guest alignment `id=1` accepts
`unknown=151`, `divergence=0`, `w1=1`; host readiness follows 16 ms later.
Independent native keyboard movement has matching reciprocal commanded/body
positions at rest (0 mm). A ground-view frame shows local Quiet and remote
Snake on foot; observations of transparency in other views do not establish
a camouflage cause or a cloak fix.

On this DLL, host checkpoint hash `0`, version `1`, pose
`(1233009,21370,-123172)` mm / yaw `0` is confirmed by the guest's actual
VarSave callback 17 ms later. This proves the in-memory save receipt;
asynchronous disk-write completion remains unobserved. Injected guest
PLAYER_DEAD reserves host shared game over after 33 ms; guest game-over
application follows another 33 ms later. Both games show Mission Failed.
Only the host then presses native menu Continue. The guest receives all
47 records and replays the same mission; both return on foot to the shared
checkpoint ground position `(1233009,20870,-123172)` mm, pair at epoch `4`
and have parts `3,3` with reciprocal transform readback 0 mm. Guest alignment
`id=2` accepts `unknown=151`, `divergence=0`, `w1=1` at `367964252`; host
readiness follows 34 ms later.

Only the host then requests fresh Afghanistan deployment. Both copies follow
Africa `30020` → ACC `40020` → Afghanistan `30010`, with 47 records on each
leg. Direct game views show Snake and Quiet seated in their own arriving
helicopters, checking the fresh-entry action after the on-foot checkpoint
case. The guest arrives at epoch `6` and accepts alignment `id=3` at
`368261214`: 271 soldiers, 77 CPs, 191 unknowns, zero divergences and
`w1=1`. Host readiness follows 32 ms later. After both helicopter exits,
the host ground view shows Snake and opaque Quiet. Native guest W for 1.5 s
and host W for 6 s have matching reciprocal transforms at rest (0 mm).

Setting `world=0` in both games logs UNPAIRED reason `5` followed by inactive
level `0`; partner transform sync continues. Both games accept `WM_CLOSE`
and exit normally at 12:08:41, with no fuse process. All ten real save/config
files match the run-start manifest and latest backup. Actual Disable then
archives both offline markers and removes the active `lab.ini`, `coop.ini`
and link secrets. The owned DLL and game/archive hashes remain intact.
Cleanup commit `9d5aac6` passes the focused `lab_scripts_tests` suite (1/1).
File-state checks and the absent-marker source gate establish inactive
pass-through; a normal launch afterward was not tested, since it would use
the real saves. Physical damage death remains untested.

The fix adds mandatory seed 28, `svars.ply_isUsedPlayerInitialAction`, for
47 records with the normal revenge array. The exact-source regression and
latest arrival/title/recovery subset pass 11/11.
Local current-build evidence: [run configuration](../build/lab/runs/20261006-115055/run.json),
[host title Continue](../build/lab/runs/20261006-115055/screens/final4666-host-continue-host-115234.png),
[on-foot bodies and movement](../build/lab/runs/20261006-115055/screens/final4666-ground-motion-guest-115433.png),
[Mission Failed host](../build/lab/runs/20261006-115055/screens/final4666-gameover-host-115746.png),
[Mission Failed guest](../build/lab/runs/20261006-115055/screens/final4666-gameover-guest-115746.png),
[Continue recovery](../build/lab/runs/20261006-115055/screens/final4666-continue-resume-guest-120017.png),
[Afghanistan ground bodies](../build/lab/runs/20261006-115055/screens/final4666-afg-bodies-host-120717.png)
and [stop/save verification](../build/lab/runs/20261006-115055/stop.json).
The [exit verification log](../build/codex-4666-live-stop-20261006.log) records
unchanged real files against both baselines. Cleanup evidence is the
[Disable log](../build/codex-final-disable-20261006.log) and
[final file-state/hash receipt](../build/codex-final-install-verification-20261006.json).

**PASS for the preceding integration subset on two game processes on one PC.** Run
`build/lab/runs/20261006-111828` uses the earlier integrated **0.11.1** DLL,
SHA-256 `171041C2A598FD758FCC67A0B19B59BE4EC6C35FBA4CD98C2529BF946A9E97D6`,
and the same local 13-entry archive as the field patch below. Both games use
our offline UDP link and separate co-op save copies. The newer diagnostic-only
build from `5ab2c5a` has DLL SHA-256
`59CD23348386815A84DA0CE9D18DB2B308E52BE21D1B5DDF9C254FC1910A9B13`;
its separate run
`build/lab/runs/20261006-113833` finds a guest initial-action mismatch:
the host starts on foot, but the guest retains its old helicopter action
although the seeded coordinates match. Both remote bodies were realized
with parts `3,3` and 0 mm transform readback error; that startup did not
establish an unobscured on-foot two-player view. Exact `TppPlayer.Init` reads
`ply_initialPlayerState=RIDEON_HELICOPTER` together with
`svars.ply_isUsedPlayerInitialAction`, which is true on the host after landing
and false in the guest's older checkpoint. The current build adds the missing
seed and regression fixture. This diagnostic-build run then closes both
games normally; all ten real save/config files remain unchanged against
run-start and latest-backup hashes. The table below records the preceding
build's larger test run; the current startup acceptance is recorded above.

| Item | Integrated result |
|---|---|
| Native title Continue | **PASS.** Only the host selected the game's title Continue. The guest followed to Mother Base `30050`. |
| Host-only mission travel | **PASS.** A `goto=30010` trigger on the host brought the guest along `30050` → `40050` → `30010`, without a guest mission trigger. |
| Complete travel seeds | **PASS.** The stream contains all 27 keys / 46 records. The guest applies title context once; later allocation clears Continue-from-title normally. |
| Afghanistan pairing | **PASS.** Both CFGs report mission `30010`, epoch `5`, roster `2772775463`, 271 soldiers and 77 CPs. |
| Initial checkpoint alignment | **PASS.** Guest snapshot `id=1` is accepted with `unknown=191`, `divergence=0`, `w1=1`. Unknown soldiers remain held until their areas can be read. |
| Afghanistan bodies and movement | **PASS.** Guest native W input for 1.5 s moved its local player and the host's partner. At rest both directions match with 0 mm position difference; screenshots show the separate Snake and Quiet bodies. |
| Partner warp rearm | **PASS.** Stage 0 → 3 with `apply=warp` places the partner before enabling its bits; realized/parts readiness and warp error 0 are confirmed. Restored `xform` / `dm=map` afterward. |
| Shared Afghanistan kill | **PASS.** Host `kill_near` selects entity `486459779`; the guest applies DEAD (`state=5`, `ver=2`) with readback `5`. This is a lab API probe. |
| Africa host travel and alignment | **PASS.** Host-only `30010` → `40010` → `30020` brings both copies to epoch `8`, roster `2220786415`, 265 soldiers and 68 CPs (`match=1`, `eff=3`). Guest alignment `id=37` accepts `unknown=157`, `divergence=0`, `w1=1`; host readiness follows 33 ms later. |
| Africa bodies and movement | **PASS.** Host's Quiet partner has parts `3,3`; guest has Snake's remote body. Native guest W for 1.3 s / D for 1.2 s and host W for 1 s show two independent actors. Reciprocal transforms match at rest with 0 mm difference. |
| Afghanistan range hide/re-entry | **PASS.** Partner hides at 200.004 m and re-enters at 149.969 m with placement before realization. |
| Exit and real saves | **PASS.** Both games close normally via `WM_CLOSE`; all ten real save/config files match the run-start manifest and latest backup. |
| Shared checkpoint | **PASS.** Host probe publishes hash `0`, version `2`, pose `(801012,339760,1638620)` mm / yaw `-60000` millidegrees. Guest's actual VarSave callback confirms the identical pose and `receipt=1` 19 ms later. This is an in-memory save receipt; asynchronous disk completion is not claimed. |
| Shared death | **PASS for the API probe.** Injected guest PLAYER_DEAD requests host authority; host reserves shared game over 32 ms later, guest applies it 33 ms afterward. Both windows show the real Mission Failed menu. Physical damage death remains untested. |
| Native host Continue recovery | **PASS with a loading delay.** Only the host pressed native menu Enter. Both games reloaded and paired at epoch `6`. Guest readiness was about 74 s behind the host; all 271 host soldiers were initially unknown, so snapshots were refused. The unchanged DLL recovered when the host area activated: guest snapshot `id=36` was accepted about 1.9 s later with `unknown=206`, `divergence=0`, `w1=1`, followed by host readiness. Transport overflow stayed zero. |
| Killed soldier after Continue | **PENDING.** Post-Continue readback for entity `486459779` is not yet recorded; checkpoint copying and alignment acceptance do not alone prove its persisted state. |

All 78 automated suites pass in aggregate: the lab installer suite was
rerun separately after its failure in the combined run. The latest
diagnostic/title/recovery subset passes 11/11. This is single-PC evidence; it does not
verify a two-PC session, identical enemy AI/positions or full campaign
progression. Initial pairing still needs both copied-save games in the same
supported scene; automatic bootstrap from different ACC/location scenes is
not implemented.

Local evidence: [run configuration](../build/lab/runs/20261006-111828/run.json),
[host title Continue](../build/lab/runs/20261006-111828/screens/final-title-host-continue-host-112027.png),
[Mother Base host](../build/lab/runs/20261006-111828/screens/final2-mb-host-host-112117.png),
[Mother Base guest](../build/lab/runs/20261006-111828/screens/final2-mb-guest-guest-112118.png),
[ACC host](../build/lab/runs/20261006-111828/screens/final-acc-host-host-112243.png),
[ACC guest](../build/lab/runs/20261006-111828/screens/final-acc-guest-guest-112244.png),
[Afghanistan host](../build/lab/runs/20261006-111828/screens/final-afghan-host-exit-host-112749.png)
and [Afghanistan guest](../build/lab/runs/20261006-111828/screens/final-afghan-guest-exit-guest-112750.png),
[movement host](../build/lab/runs/20261006-111828/screens/final-afghan-motion-host-112811.png)
[movement guest](../build/lab/runs/20261006-111828/screens/final-afghan-motion-guest-112811.png),
[Mission Failed host](../build/lab/runs/20261006-111828/screens/final-shared-gameover-host-112940.png)
[Mission Failed guest](../build/lab/runs/20261006-111828/screens/final-shared-gameover-guest-112940.png),
[Africa bodies](../build/lab/runs/20261006-111828/screens/final-africa-bodies-host-113742.png)
and [stop/save verification](../build/lab/runs/20261006-111828/stop.json).
Automated evidence: [combined suite log](../build/codex-recovery-final-20261006.log),
[serialized lab recheck](../build/codex-recovery-lab-recheck-20261006.log),
[title/recovery checks](../build/codex-title-recovery-final-20261006.log),
[diagnostic checks](../build/codex-alignment-diagnostic-tests-20261006.log),
[current arrival checks](../build/codex-arrival-fix-tests-20261006.log)
and [preceding build exit/save log](../build/codex-d171-live-stop-20261006.log).
These artifacts remain local under `build/`.

## October 6: free-roam partner field patch

**PASS for the field patch on two game instances on one PC.** Run
`build/lab/runs/20261006-095606` used our own offline UDP link, separate save
copies, `partner_apply=xform`, `look=net` and `dm=map`. The extended partner
changes are source commits `7ec1e39` and `07520e0`. The tested DLL still had
the **0.11.0** label; `a4a5aab` later changed the source/package version to
**0.11.1**. W3/W4 were not integrated into this tested DLL.

Both installed archives matched SHA-256
`87BBE82BB1E907CF405200155DA2794BF7E1F5A3D45BF66426A38CDD31A91F96`:
13 entries, 16,333,824 bytes. The archive retains the previous Mother Base
assets and adds normal Afghanistan/Africa replacement packs, with the
appropriate scarf/normal two-player controller. Each supported mission
replaces its original controller; it does not append a second controller.

| Item | Field result |
|---|---|
| Afghanistan `30010` | **PASS.** Both copies showed two player bodies. Guest keyboard movement matched the host's partner body to about 20 mm. |
| Distance safety | **PASS.** Both copies hid a partner at 200 m. |
| Africa `30020` | **PASS.** Both copies selected the normal replacement pack, loaded two IDs with parts states `3,3`, and showed movement command/readback differences of 0–18 mm. |
| Exit and real saves | **PASS.** Both games accepted `WM_CLOSE` and exited normally. All ten real save/config files were unchanged against both the run-start manifest and latest backup. |

Local evidence: [run configuration](../build/lab/runs/20261006-095606/run.json),
[stop/save verification](../build/lab/runs/20261006-095606/stop.json),
[Afghanistan host view](../build/lab/runs/20261006-095606/screens/afghanistan-body-motion-host-100917.png),
[Afghanistan guest view](../build/lab/runs/20261006-095606/screens/afghanistan-body-motion-guest-100917.png),
[Africa host view](../build/lab/runs/20261006-095606/screens/africa-partner-motion-host-101546.png)
and [Africa guest view](../build/lab/runs/20261006-095606/screens/africa-partner-motion-guest-101546.png).
These generated artifacts remain local under `build/`.

This earlier run verifies supported field bodies and movement for its
pre-integration DLL. The integrated run above records travel, alignment, Afghanistan movement,
checkpoint, injected death and delayed native Continue recovery evidence.
The current DLL's separate run above verifies Africa and Afghanistan
bodies/movement, checkpoint copying and native Continue recovery. The
previously killed soldier's post-Continue readback remains unverified.
These runs do not establish a two-PC session or full campaign gameplay. Story
and alternate packs such as `30011` remain excluded from partner-body support.

## October 5: W0–W2 live results

Both lab copies on one PC, offline, link v2, lab.ini `world=3`. Builds:
coop-lab e4eabee (merge) plus the fixes below (final 19c0aba). Real saves were
UNCHANGED after every run.

## Results

| Item | Result |
|---|---|
| W0-4, `world=0` inert | **PASS.** No `world` line in either log, the partner unchanged (look, dm=map clips). |
| W0-1, pairing in 30050 | **PASS.** `world=paired; ...; mission=30050; roster 2118791974 both; soldiers 126; cps 7; match=1; eff=3`. |
| Lab goto | **PASS** after two fixes (below): 30010 -> 40010 (ACC) -> 30010 in about 70 s on both copies, no input, `w1=1` on arrival. |
| W0-3, roster in 30010 | **PASS.** `roster 2772775463` both, 271 soldiers, 77 CPs, `match=1`. |
| W1-1, host kills A | **PASS.** Host `commit ...;state=5;ver=1;actor=host`; guest `apply ...;src=outcome;result=ok;readback=5` 230 ms later; exactly one guest `m=Dead`. |
| W1-2, guest tranquilises B | **PASS.** Guest `req`, host `commit ...;actor=guest` (50 ms), host `apply ...;result=ok;readback=2`, guest `result=noop`. |
| W1-3, guest fultons C | **PASS** after three fixes (below): guest `req ...;state=6`, host `commit` (50 ms), host `apply ...;result=ok;readback=6` about 1.2 s later; one `FultonInfo` per PC. |
| W1-4, race | **PASS.** Both fired at the same millisecond; host one `commit ...;ver=1`; guest `req_done ...;result=reject;reason=2`, `apply ...;result=terminal_local`; no `ver=2`. |
| W1-5, guest alert | **PASS.** Host CP to phase 3 within about 50 ms (`src=req`), `event=floor;host=3;guest=3` every 10 s; host escalations (caution on neighbouring CPs) applied on the guest. |
| W1-6, out-of-area kill | **PASS.** Guest `ledger op=hold;reason=out_of_area`, then `applied;waited_ms=6022` and `apply ...;result=ok;readback=5`. |
| W2-1, clock | **PASS.** Guest `clock_check;diff=` -1..-2 game seconds. |
| W2-2, weather | **PASS.** Rain on both screens; the guest's check reached `local=2;host=2` after the transition (about 45 s; the +20 s check still read a transition value). |
| Probes | p1 `SetPhase` applies at once and emits ChangePhase; p3 `NPC_LIFE_STATE` dead=1 dying=2 sleep=3 faint=4 (FAINT readback 4; the design critique's "4 is DEAD" guess was wrong, so runtime lookup matters), applied deaths carry `att=null`; p4 an out-of-area soldier is `active` 2 s after its area loads; p12 offline `IsHost()=true`. |

## Fixes made during the run

1. `mission_terminating` was never cleared offline after the first mission
   exit, so the world CFG start was refused in every later mission (40050,
   30010). It is now cleared when a new mission's runtime begins (1b75b89).
2. Lab goto: it can also arm in an ACC (step 2 only, fb6938a) and in free roam
   (step 1 = `TppMission.ReserveMissionClearOnRideOnHelicopter()`, b12f6b6),
   so a continued free roam can be restarted fresh. The ACC's "Overall
   Completion" title screen needs Enter, not Space.
3. Fultons of ordinary soldiers: `TppEnemy.IsRecovered` only covers the
   mission's recovered-state list, so the guest never requested them. The
   local state now also reads `GetActionStatus == ACTION_STATUS.FULTON_RECOVERD`
   (5d5ec6f), the Neutralize that follows FultonInfo no longer cuts the
   pending fulton read short (e2c6030), and the host's fulton readback uses
   the same state (19c0aba).

## Limits of the October 5 build

- After a **continue** (or checkpoint reload) W1 stays off for the mission
  (`event=inactive;reason=continue`); clock and weather still sync. Real play
  usually starts with a continue, so W4 (checkpoint snapshot) is the next
  priority for enemies to be shared from the first minute.
- That build only showed the partner body in Mother Base (30050). The
  October 6 field patch above extends normal 30010/30020 packs. The later
  integrated run above verifies host-led Mother Base–ACC–Afghanistan travel.
- `remote_area_kill` is a probe-only commit: the host's own soldier stays
  alive (only the guest applies it).

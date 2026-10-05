# World sync W0-W2: live lab tests (2026-10-05)

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

## Known limits (by design until W3/W4)

- After a **continue** (or checkpoint reload) W1 stays off for the mission
  (`event=inactive;reason=continue`); clock and weather still sync. Real play
  usually starts with a continue, so W4 (checkpoint snapshot) is the next
  priority for enemies to be shared from the first minute.
- The partner body only appears in Mother Base (30050); W3 lifts that and
  makes the guest follow the host's mission changes.
- `remote_area_kill` is a probe-only commit: the host's own soldier stays
  alive (only the guest applies it).

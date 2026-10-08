# Partner weapon poses: motion-graph deep spike

October 8, 2026 spike (SQ-4), based on `dev/oct7-continue` d678460. This is
static research only. No game was launched, no loader was installed and nothing
under the game folder was written. Runtime statements cite earlier recorded
runs. No product code changed.

Inputs:

- `TppPlayer2_layers.mog` from `0/00.dat`, sha256 `5353cec1...6b31` (copy in
  `build/partner-turn-resident-audit-20261007/package-00_fpk`)
- the resident, `player2_online`, `player2_pipe` and four receiver MTAR copies
  listed in the October 7 spike (SQ-2,
  `git show 85f3f70:docs/partner-weapon-poses-2026-10-07.md`)
- H6 local Gun/MotionVars captures in
  `build/action-channel2-h6-delayed-writer-20261007/host-local-sources-*.json`
- `mgsvtpp.exe` sha256 `085c2f82...bb45`, read only

Hashes were computed offline with the repository's Go module
`github.com/unknown321/hashing`, using `StrCode64` for node names and
`HashFileNameWithExtension` (PathCode64) for clip paths. Scripts and outputs are
in the board evidence folder
`~/.claude/sidequest/projects/mgs-coop-d08682c0/verification/SQ-4`
(`mog-refs.json`, `mog-graph2-3.txt`, `names.txt`, `receiver-named.txt`,
`h6-local-sources-labelled.txt`).

## Findings

| # | Question | Finding | Evidence | Confidence |
| --- | --- | --- | --- | --- |
| 1 | How does the MOG reference clips? | Graph nodes are 0x48-byte records. Node names and clip PathCode64 values sit in one shared pool (`0xa6828..0xb2ec0`) and are reached through signed 32-bit relative pointers. The pool holds 1424 clip hashes that are found in our MTARs: 1213 resident, 159 `player2_online` and 52 `player2_pipe`. It holds 1016 more gani-type hashes that are not in any MTAR we have extracted. | `mog-refs.json`. All 1424 have inbound relative pointers. | High (byte scan) |
| 2 | Which upper-graph nodes are the gun states? | Graph 2 has 122 nodes. Names recovered by StrCode64: `stateGunHold` n78, `stateGunHoldStart` n33, `stateGunHoldEnd` n103, `stateGunFire` n65 (type 6, a container whose child is unnamed n116), `stateGunReload` n5, `stateGunCock` n17 and `stateNull` n89. The throw, place and arm-action families are also present. | `names.txt`. These indices match the runtime states: aim 78, Fire child 116, reload 5 and idle 89 ([H3/H6](testing-2026-10-07-animation-debug.md)). | High |
| 3 | Which clips do those states reference? | `stateGunHold` has one static clip, `fc510e5a354b65b1` (resident). `stateGunHoldStart` has 11 resident clips (raise and transition). `stateGunReload` and Fire child 116 have **no static clip**. No receiver-MTAR hash is referenced anywhere in the MOG. | `mog-graph2-3.txt`. Clips are assigned to the nearest preceding node payload, which is a layout heuristic. | Medium-high |
| 4 | Where do fire, reload and aim clips come from? | They come from Gun variables that the gun service writes at runtime. During H6 the local player's sources held `fc53958377e25e9f` = **`snapar01_aw0_s_fre`** (fire; sources 6/10/21 always, source 11 while firing) and `fc51dacb4460c33d` = **`snapar01_aw0_s_fre_rld`** (source 11 after the reload). Source 11 held resident idle `fc503e7bcae09172` at rest. Source 7 always held `fc5310694a89c696`, which is in no extracted MTAR and not in the MOG. | `h6-local-sources-labelled.txt`, `receiver-named.txt`. Hashes were reproduced from the exact paths. | High |
| 5 | Per weapon class | The receiver MTARs hold the class clips. Named entries: hg00 `snaphg00_s_fre`, `_s_fre_emp_rld`. ar00 `snapar00_aw1_s_fre`, `_aw0_s_fre_rld`. ar01 `snapar01_aw0_s_fre`, `_aw0_s_fre_rld`. sr02 `_s_fre1`, `_s_fre2`, `_s_fre_rld`, `_s_fre_emp_rld`. Each of hg00/ar00/ar01 has 5 unnamed entries, and `e15b0f433f7653fc` is shared by ar00/ar01. No crouch or prone clips were found by name. The graph applies stance through its own layering. | MTAR entry tables (in `h6-local-sources-labelled.txt`) | High for named entries |
| 6 | Full body or upper only? | Layering belongs to graph 2, the upper layer. The clip itself is the same `.gani` that DM can name. Its lower-body track content was not decoded. Clip 29 (`snapsr02_s_fre2`) through graph-0 DM showed raised arms with **normally standing legs**, and it ended naturally. | [run 20261006-175724 frame](../build/lab/runs/20261006-175724/screens/actions-e-gun29-guest-180113.png) | Medium (one rear view) |
| 7 | Resident for instance 1? | Upper-graph static clips are resident. The receiver clips are present only with the **31-motion research archive** `2E314419...C9A`, whose catalogue allowance is 0..30. That archive loads whole MTARs, so `snapar01_aw0_s_fre_rld` is present in it but has no catalogue index. The normal 21 archive allows 0..20, which contains no weapon clip. | [archive allowance](testing-2026-10-07-animation-debug.md) | High |
| 8 | Can DM take a hash? | **No, not from Lua.** `RequestToPlayDirectMotion` takes a string. The per-instance DM record (stride 0x250) stores a 64-bit clip code at slot+0x38, or a state code at slot+0x78 for the named-state branch. Consumer `1199140` masks the state code to 48 bits and compares it with two globals. It then calls the controller's vtable +0x190 (play the clip in the generic DM state) or +0x170 (request a named state). A Lua 5.1 number cannot carry a 64-bit PathCode exactly. A native write of slot+0x38 is unverified and is **not needed**, because the paths for the fire/reload clips are now known. | `build/upper-direct-motion-graph-audit-20261007/dm_path_vs_named_consumer.txt`, `dm_consumer_file_store.txt` | High for the Lua limit |
| 9 | Do state names work in DM (catalogue 21..25)? | They do not select the upper states. The named-state branch requests the state on the lower graph-0 controller, and `stateGun*` exists only in graph 2. This matches H2, where no upper pose appeared until the channel was redirected. | Findings 2 and 8, [H2](testing-2026-10-07-animation-debug.md) | High |
| 10 | Aim loop? | No DM-playable aim loop exists. Retail aim keeps `stateGunHold` running with the fire clip in the Gun variables plus unknown source 7. DM cannot hold a clip at a frame, so it can only replay fire/reload one-shots. | Finding 4 | Medium-high |

### Gun in hand (task 3)

| Candidate | Evidence | Status / qualification needed |
| --- | --- | --- |
| `EquipControllerImpl::SetCurrentActiveWeaponSlot(instance, slot)` | Method name and signature only, from `work/upstream-tpp-mod/src/client/game/structs.hpp` (EquipControllerImpl_tpp vtable). It takes an instance index. | **Not callable.** Its address is known only from a community table. Before any use it needs a fingerprint, the vtable slot in the live controller, its calling convention and its lifecycle for a non-local instance. The in-hand model would probably still need gun activation `1099AB0 -> 105FDB0`, which runs on the scheduler. |
| `SetupWeaponToTemporarySlot(instance, slot, req, n)` / `RestoreWeaponSlotFromTemporarySlot` | Same struct. Retail uses temporary weapons for pickups and mission weapons. | Not callable. Same qualification, and the effect on a remote body is unknown. |
| `SetVisibilityToEquip(instance, req)` | Same struct | It controls visibility, not attachment. It might hide the back rifle during a fire cue. Not callable. |
| `Player.ChangeEquip` (public wrapper `9D0D60`, instance index in R8D) | Verified path in [remote-equipment contract](evidence/remote-equipment-contract-2026-10-07.md). Retail Lua (s10050, s10151, s10240, s10260) calls it only for the local player. | The verified stock46 writer already reuses this resource path. Its result is the rifle **on the back**, not in the hands (H7). |
| `Player.SetEquipState`, `Player.UnsetEquip` | Retail s10240/s10260: suppressor and light flags, removing slots | They do not attach anything to the hand. |
| Demo or cutscene prop attachment | No player-hand prop API in the binding names we have. A demo owns its own models and takes over the camera and players. | Not usable during gameplay. |
| `CopyPartsInfoFromLocalPlayer`, `SetPartsInfoAtInstanceIndex`, `SetExtendPartsInfo` | They set body parts, camo and the hand type (prosthetic arm). | No weapon model. |

**Conclusion:** no verified, rules-compliant way exists to put a weapon model
in instance 1's hand. Every in-hand candidate is either an EquipController
method known only from a community struct, or it depends on the gun-activation
scheduler that the rules exclude.

## Recommended route

The direct-motion one-shot route from SQ-2 remains the only rules-compliant
route. This spike makes it more exact:

1. For a hip `ar01`-class weapon, use **`snapar01_aw0_s_fre` (GANI 28)**. It is
   the exact clip the retail upper graph plays when firing (finding 4). SQ-2's
   `partner_gun.hpp` (on `refs/sidequest/SQ-2`) maps hip to 27
   (`snapar00_aw1_s_fre`). Choose 28 when the remote's hip weapon is an ar01
   receiver.
2. Add **`snapar01_aw0_s_fre_rld`** as catalogue index 31 for the hip reload.
   It is already present in the research archive. This needs
   `kPartnerDmGaniMax` 31, the 32-bit bad mask, the archive allowance 0..31 and
   matching tests. It is not implemented here; see "Not implemented".
3. Aim gets no clip. The existing locomotion and turn clips stay.
4. Hands remain empty, and the stock46 rifle stays on the back. This is a
   **visual cue without a weapon**.

What is blocked, and why:

- **A weapon in the hand:** there is no verified per-instance attach path, and
  the scheduler route is excluded by the user's rules.
- **True upper-body layering while walking:** this needs graph 2 with a
  populated Gun variable block for body 1. That block is written by the gun
  service on the animation worker, which is excluded (no worker hooks and no
  scheduler driving).
- **The aim loop:** it has no standalone clip, and source 7 (`fc5310694a89c696`)
  is unlocated.
- None of the blocked items needs S_IS_ONLINE, FOB, sessions, Steam P2P,
  master-server, version or fuse changes. They are blocked only by the
  animation-worker and verification rules.

## Hidden-worker experiment for the coordinator

Purpose: compare the exact retail upper fire clip (28) with clip 29 on body 1,
judge the legs and arms, and confirm a natural finish and the return to idle.

**Prerequisite (not runnable today):** `tools/worker/Start-WorkerSession.ps1`
pins the Baseline21 archive. It checks the size `16333824` and writes
`runtime_motion_count='21'`. With the normal archive the allowance is 0..20, so
`dmplay=26..30` is refused. The coordinator must first add a
`-RuntimeArchiveProfile normal21|receiver31` switch. For `receiver31` it pins
the research archive `2E314419B3E42794E5B89A253AD767D28A1E6834F88AAE22DF5D0DEDFED66C9A`
(16,944,128 bytes, `build/partner-gun-preload-candidate-v2-20261006`), writes
`runtime_motion_count='31'`, and keeps the owned install, rollback and save
checks. This is an admission and safety change to the worker, so it was not done
in this spike.

No new `lab.ini` keys are needed. Use the animated profile (`partner_dm=map`)
as in the existing runs. The manual `partner dm=on` command overrides the map
for the request.

Scenario (`tools/worker/scenarios/partner-gun-clips.txt`, using the
`Invoke-WorkerScenario.ps1` grammar):

```
# Body-1 manual DM of retail receiver clips (needs receiver31 archive profile).
title 200
wait 4000
waitgameplay host 120
waitgameplay guest 240
stance host stand
stance guest stand
host debug time 43200
host debug weather sunny
wait 2500
placebehind guest host 4 3
both capture start
guest debug partner dm=on dmplay=28
guest burst dm28-ar01-fire 8 150
wait 1500
guest capture dm28-after
guest debug partner dm=on dmplay=29
guest burst dm29-sr02-fire2 8 150
wait 1500
guest debug partner dm=on dmplay=26
guest burst dm26-hg00-fire 8 150
wait 1500
guest debug partner dm=on dmplay=30
guest burst dm30-sr02-reload 10 200
wait 2500
guest capture idle-return
state
```

Pass criteria, from the guest's `captures.jsonl` and log:

- the debug results for 28, 29, 26 and 30 are accepted rather than refused as
  unavailable
- `partner_dm=play; gani=28` and so on in the log
- the frames show raised arms with standing legs, with no collapse and no
  T-pose
- each clip finishes on its own, and `idle-return` shows the idle loop again

A second run with the stock46 writer ON (`MGS_LOCAL_STOCK46_PRELOAD_TEST` build)
would show the combined look: a firing pose with the rifle on the back.

## Not implemented

No default-off catalogue or config addition was made. The experiment needs the
worker archive profile above (an admission change), and the reload index 31
would touch the catalogue bound, the bad mask, the archive allowance, the F9
menu and the composition fixtures. Neither is a cheap isolated addition, so both
are left for the coordinator.

## Open items

- Locate `fc5310694a89c696` (Gun source 7 during aim). It may be the hold
  pose. It is not in any extracted MTAR. Finding it needs a dictionary or a
  broader read-only archive extraction.
- Name the 5 unnamed entries in each of hg00/ar00/ar01. This needs a community
  MTAR dictionary (a source lookup) or targeted guessing.
- Decode receiver clip track masks to settle full-body versus upper-only
  content.

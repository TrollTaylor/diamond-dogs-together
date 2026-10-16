# Private desktop worker

This is an original Win32 launch supervisor and bounded hardware DX11 fixture.
It is separate from the normal mod. The supervisor and probe use the GUI
subsystem and create no console. No worker code calls `SwitchDesktop`, system
input injection, foreground/focus APIs, or changes GPU timeout settings.

MGSV reads input only while `GetForegroundWindow()` is its own window, which a
window on a never-displayed desktop never is. In the worker build only, the
in-process bridge answers `GetForegroundWindow()` for admitted game threads on
the private desktop with the game's own private window (the validated
`SetCooperativeLevel` target, else the process's top-level window found on that
desktop). This is a process-local answer: nothing calls `SetForegroundWindow`,
activation or focus APIs, and the user's desktop is unaffected.

The coordinator alone builds, scans and admits runtime artifacts. Compilation
and pure fixtures do not qualify a retail-game run. First qualify the probe;
then independently qualify MGSV startup, continuing simulation, virtual input,
audio/cursor isolation and captured animations. A private desktop separates
windows within the same user/session; it is not a security or GPU sandbox.

## Build integration

CMake now integrates the `worker_supervisor`, `worker_probe` and
`worker_protocol_tests` targets. `MGS_BACKGROUND_WORKER` is a separate,
default-OFF option; normal packaging explicitly disables it. Use
`Invoke-WorkerChecks.ps1 -EnableBackgroundWorker` to build and audit the
dedicated Normal/worker-ON profile without changing `build/native`.

Both worker binaries are x64. The protocol test performs no OS calls. Running
the probe is a separately admitted private-desktop runtime fixture, not an
ordinary CTest test. Do not use existing visible `Start-Lab`, `Send-LabInput`
or desktop screenshot helpers to drive the worker.

## Coordinator protocol

Supervisor CLI is exactly `worker_supervisor.exe --launch <manifest>`.
The manifest is a UTF-8 `key=value` file with a final newline, no comments,
duplicate/unknown keys, BOM or control characters. Paths are canonical
drive-absolute backslash paths shorter than 240 UTF-8 bytes; UNC/device paths,
ADS, relative segments, trailing aliases and `userdata` components refuse.
The manifest lives inside its new, existing run directory. Ancestors are
checked for reparse points and held without delete sharing. A fresh exclusive
`supervisor.lock` and per-run named mutex prohibit run reuse/adoption.

Common manifest keys:

| Key | Value |
| --- | --- |
| `schema` | `1` |
| `mode` | `probe` or `mgsv` |
| `run_id` | exactly 32 lowercase hexadecimal characters |
| `run_dir` | existing owned directory for this run |
| `desktop` | exactly `MGSWorker_<run_id>` |
| `timeout_ms` | 2000 through 600000 |
| `ready_timeout_ms` | 1000 through 180000; at most `timeout_ms` |

Probe mode additionally requires absolute `probe_exe` and its complete
`probe_sha256`. The child receives only fixed `--probe`, never manifest-provided
arguments. It tests three 64x64 red hardware frames, an RGBA staging readback,
windowed swap-chain state, adapter identity and Present HRESULT. A successful
probe may report `DXGI_STATUS_OCCLUDED`; that explicitly does not prove MGSV
would keep simulating or rendering there.

Probe outputs are `probe.json`, `probe.context`, `probe-backbuffer.bmp` and,
only after successful child exit/admission, `qualification.txt`. The probe keeps
its window and DX11 resources alive while publishing readiness. The supervisor
strictly inspects the live process, threads, job and private windows, then writes
`probe.ack` for that exact run/PID/creation time. Only that acknowledgment allows
teardown. Both waits are bounded to ten seconds, message draining is capped,
and qualification still requires exit 0 and the unchanged pinned bitmap:

```text
MGSWORKERQUAL1 <probe_run> <supervisor_sha256> <probe_sha256> <bitmap_sha256> <tick64> qualified
```

The qualification is bounded to the same supervisor binary and a nonfuture
tick age of at most one hour. Tick comparison alone is not a unique boot
identity: the coordinator must qualify a fresh probe in its current session.
It is a capability receipt, not a security token.
The coordinator pins its full file hash into the later MGSV manifest. Its
location must be under the parent of the new run folder and outside that run.

MGSV mode additionally requires:

| Keys | Required values |
| --- | --- |
| `exe_sha256`, `steam_sha256` | exact retained retail hashes in `worker_protocol.hpp` |
| `loader_sha256` | admitted loader artifact hash, shared by both roles |
| `runtime_archive_sha256`, `runtime_motion_count` | baseline archive `87BB...91F96`, catalog allowance `21`; research profiles refuse |
| `probe_receipt`, `probe_receipt_sha256` | prior `qualification.txt` path and full file hash |
| `offline_master_port` | 1024 through 65535; no TCP listener at each launch observation |
| `host_exe`, `guest_exe` | absolute `mgsvtpp.exe` paths in distinct, nonoverlapping game roots |
| `host_save_dir`, `guest_save_dir` | existing, distinct nonoverlapping descendants of `run_dir` |
| `host_lab_sha256`, `guest_lab_sha256` | hashes of each `mgs-coop/lab.ini` |
| `host_coop_sha256`, `guest_coop_sha256` | hashes of each `mgs-coop/coop.ini` |
| `host_graphics_sha256`, `guest_graphics_sha256` | hashes of each copied `287700/local/TPP_GRAPHICS_CONFIG` |

The supervisor also checks `script-observe.enabled` is exactly `offline-v1`
with LF only (matching the native observer), lab role/mode/loopback, distinct mutex suffixes, guest identity,
`xinput=none`, exact copied-save path, the closed-port loopback master URL and a
single pinned `window_mode` value `Windowed`. Partner/net must be enabled with
source/input `net`, apply `input`, input sending enabled, and exact 127.0.0.1
binding/peer addresses.
The baseline archive is pinned at its exact 16333824-byte size. The in-process
bridge must independently verify actual mounted catalog count 21.
The coordinator must verify the
graphics configuration tag and seed actual save copies before this invocation.
Both `287700/local` and `remote` copied-save folders must already exist.
The graphics read pin is released after checking so the game may update its
own copy. Other pinned immutable files remain open without write/delete sharing.
Real save hashes, installation ownership and restoration remain coordinator
responsibilities; environment declarations do not prove save redirection.

Child environment replaces inherited `MGS_WORKER_*` entries with:

- `MGS_WORKER_RUN_DIR`, `MGS_WORKER_RUN_ID`, `MGS_WORKER_ROLE`;
- `MGS_WORKER_DESKTOP`, `MGS_WORKER_GAME_DIR`.

MGSV also receives `MGS_WORKER_MOTION_COUNT=21` and the admitted
`MGS_WORKER_ARCHIVE_SHA256`; these declarations do not prove a native mount.

Each child is created suspended with `STARTUPINFOEX.lpDesktop` naming
`WinSta0\MGSWorker_<run_id>`, `bInheritHandles=FALSE`, and child-creation
restriction. The supervisor assigns its nonbreakaway job and verifies PID,
creation FILETIME and image path before resuming it. A suspended thread's
desktop query is diagnostic only: USER/GDI desktop connection may occur
later. The child's own context and connected private windows provide
desktop admission evidence after startup.
The job has kill-on-close and desktop/display/system-parameter/clipboard/exit
UI restrictions. Nested-job incompatibility refuses; there is no visible
fallback. Child restriction is defense in depth, not a full broker boundary.
An external Steam broker is never adopted into ownership.

MGSV readiness requires the in-process bridge to atomically publish:

```text
MGSWORKERCTX1 <run_id> <host|guest> <pid> <creation_filetime> <desktop> admitted
```

The bridge must publish this only after actual executable/offline/copied-save
and installed isolation-hook admission. `host.context` and `guest.context`
are checked against the supervisor's own process handles. Probe uses the same
seven-token shape with magic `MGSWORKERPROBE1` and role `probe`.

The supervisor persistently publishes atomic `supervisor.json` with stage,
reason, deadlines, pinned identities, owned children, input-desktop observations
and forced-cleanup result. The coordinator can atomically replace
`supervisor.control` with the sole accepted command:

```text
MGSWORKERCTL1 <run_id> <increasing_sequence> shutdown
```

Malformed, foreign and repeated controls authorize no actions. Shutdown sends
`WM_CLOSE` only to rechecked owned private-desktop windows, waits up to five
seconds, then terminates only its owned job if needed. Timeout follows the same
path. Unexpected ownership/desktop failure immediately closes the owned job.
Forced termination is explicit and may leave partial copied-save writes.

The probe verifies `JOB_OBJECT_UILIMIT_DESKTOP` and observes the documented
CreateDesktop denial before creating its graphics window. It never invokes
SwitchDesktop to test a restriction whose failure could expose the desktop.
`switch_block_basis` labels the documented inference honestly.

### Debug commands and the worker-drawn F9 menu

`<role> debug <command>` stays fire-and-forget. `<role> run <command>` waits for
the command's own final receipt: `state.json` (schema 2) records `queued_kind`
(`command` or `menu`) and `queued_ref` (the command id or overlay serial) for the
accepted sequence, so interim results such as `submitted_warp` are never taken as
the answer. Queue refusals return `QUEUE_REFUSED: ...`, send failures
`SEND_REFUSED: ...`. `<role> expect <regex>` checks the role's last `run` result;
mismatches are listed in `ExpectFailures` and the run continues.

Text starting with `menu ` never reaches the game queue. In worker builds it drives
the overlay drawn into the captured frames: `menu open [category]`, `close`,
`toggle`, `category <player|enemies|partner|world|tools>`, `search [text]`,
`press <action_id>`, `confirm`, `cancel`, `footer 0|1`, `select <hash>`. The
overlay is drawn without the Win32 backend, never takes mouse or keyboard input
and draws before the capture of the same Present. No F9 key is ever sent.
`scenarios/debug-menu.txt` exercises every category; see `docs/debug-menu.md`.

Partner profiles (`-PartnerProfile`): `animated` (default; xform + dm=map), `input`
(round-4 input drive) and `native` (the native puppet, `docs/native-puppet.md`: the
animated profile as the automatic fallback plus `partner_drive=native`). `native`
takes `-NativeStage census|loco|gun` (default `loco`); `gun` needs
`-BuildProfile research` (the Research worker build with exactly the stock46
preload and writer experiments, optionally `-AmmoExperiment`; no `upper_*`
experiment), whose cache is the Invoke-WorkerChecks research directory for that
experiment set. The settings of each profile and stage come from
`Get-WorkerPartnerSettings` (WorkerSessionCommon) and the supervisor admits only
them (`worker_game_gate.hpp`). `scenarios/partner-native.txt` is the native
evaluation; its header lists the evidence each stage must show in captures.jsonl.
`scenarios/partner-native-actions.txt` covers the movement actions (sprint
parity with the rates mirror, sprint stop, crawl, dive, evade, prone rolls, an
ACTION census and a vault probe) on the loco or gun stage. Both native loco and
gun stages pass EVADE (masks 0x2240006 / 0x224005E) and run
`partner_native_rates=mirror`; `worker_session_tests` checks every shipped
scenario against the runner's own step validation and that the gate compares
exactly the stage masks.
The gun stage runs visual fire (`partner_input_mask=0x224005E`,
`partner_native_fire=visual`: the partner's shots are animation, sound and
effects on the receiving machine, never bullets; FIRE stays masked whenever the
RequestBullet owner filter is not live) and the magazine mirror;
`scenarios/partner-native-weapons.txt` is its evaluation (fire, held fire,
reload aiming and holstered, weapon switch, crouched fire, both directions,
leak and noise records).

Online play (`docs/native-puppet.md` "Online"): `-WanSim off|good|bad|awful`
puts a lab.json wanSim preset on both copies in either mode; `-LabSetting`
also takes `partner_native_net=lab|online`, `partner_native_stall=neutral|hold`,
`partner_native_hold=own|net` and `partner_input_edge_repeats=2..8` (the
allow-list is identical in Start-WorkerSession and Invoke-WorkerScenario;
`worker_session_tests` compares them). `-Mode play` is the play rehearsal
(`docs/two-pc-play.md` "Hidden play rehearsal"): both copies in `mode=play` as
Play-Coop writes it, moved onto 127.0.0.1 with the worker-only
`play_rehearsal=1`, the real identity and a fresh link secret; the manifest's
`lab_mode=play` makes the supervisor admit it with `PlayRehearsal`, and the
startup verdict is `Get-WorkerPlaySafetyVerdict`. Profiles there: `animated`
(clips) or `native` loco|gun (`partner_native_profile`).
`scenarios/play-native.txt` and `scenarios/partner-native-online.txt` hold
the evidence.

The step `stall <host|guest> <ms>` (50..1000 ms) is a worker-only input stall
(`docs/anim-round2-throws.md` "Stall test step"). The runner sends the native
debug command `input_stall <ms>`, and that role's game then sends no partner
input frames for that long (its edge sets go out with the first frame after
it). The runner records the command's result as an expectation
(`^input_stall=started;ms=<ms>;`), and the role's log has
`partner_net=input_stall; reason=start` and `reason=end` lines. The game admits
the command only in a background-worker build with the owned private worker
active (`worker_scenario::Compiled()` and `Active()`, copied saves, partner and
net on). Only that build compiles the stall into `partner_net`, which refuses
with `not_worker_build` everywhere else, so the friend/Release loader (worker
OFF by its receipt rules) never pauses input. Use it to put a gap of a known
length inside a held action, independent of `-WanSim`:
`scenarios/partner-support-stall.txt` puts two 320 ms host stalls inside a held
grenade (the stall freeze lies between stale_ms 250 and stall_ms 400).

`scenarios/steam-selftest.txt` (header `labsetting steam_selftest=1`; the
runner also allows `steam_selftest=2`, which adds a network-loopback socket
pair that binds UDP sockets) is the Steam online self-test
(`docs/steam-online.md` section 14). The host (real identity) runs the
loader's gates and an in-process Steam socket pair carrying two link v2
sessions, then inert Messages calls; it never sends to a peer. The guest
(the lab's fake identity) must refuse. `host run steam_status` /
`guest run steam_status` read the verdict and state.json gains a `steam`
member. Before it: `python tools/generate_steam_shim_contract.py --check`
and `--steamclient "<Steam folder>/steamclient64.dll"`, then the worker
checks.

### Save handoff and travel lock (join-fix phase 2)

`docs/join-fix-phase2-spec.md` P2-19 and track E. These steps never drive
gameplay beyond Enter at a title; they read owned files, wait for one role's own
exit and apply a staged save inside the run folder.

Seeds: `-SaveSeed real|lab` still seeds both copies. `-HostSaveSeed` and
`-GuestSaveSeed` (`real`, `lab`, or `''` for `-SaveSeed`) seed one role.
`-GuestSaveSeedRun <runId>` seeds the guest from an earlier, closed run's
`guest-saves\{287700,remote}`. The source must be an exact
`build\worker-runtime\<runId>` folder with `session.json` status `closed`. It is
read only: links, more than 4096 entries or oversized files refuse, and its
`287700\local` must hold a `TPP_GAME_DATA` file. It cannot be combined with
`-GuestSaveSeed`. `session.json` records `save_seeds` and each role row's
`save_seed_kind` / `save_seed_run`. `-LabSetting` (and the scenario
`labsetting` line, all three allow-lists identical) also takes
`save_handoff=off|stage|scene|always` and `travel_lock=0|1`.

Steps:

- `titlemenu <host|guest|both> <s>`: Enter every 3 s only while the role is at
  the boot screens (`state.json` mission 0 or 1, or no state yet). It stops at
  40010/40020/40050. The role's `loading.json` names the next mission at
  allocation start, about 10 s before a title takes input, so no Enter lands on
  the title menu (where it would be Continue). Any other mission fails the step.
- `title <host|guest> [s]`: the old `title` loop for one role only.
- `waitlog <role> <regex> <s>`: a complete `script-observer.log` line of this
  session (from `session.json` `roles.<role>.log_offsets`) must match the
  case-sensitive regex. Wrap a regex with spaces in single quotes. A timeout is
  an `ExpectFailures` entry; the scenario continues.
- `expectstate <role> <field> <regex>`: a fresh (3 s) `state.json` field of an
  allow-listed set (mission, generation, gameplay_ready, terminating, world_*,
  handoff, handoff_pct, travel_unlocked), nonfatal like `expect`.
- `waitexit <role> <s>`: the role's leased game process must end on its own
  while the supervisor keeps running. The exit is recorded in `session.json`
  (`roles.<role>.exited`, `forced=false`) and in `scenario.json` `ExitedRoles`.
  Later steps that drive that role fail fast; `waitlog` and `applyhandoff` still
  work. Stop-WorkerSession then tolerates the role: it sends nothing to it, and
  a reused PID is not adopted. The baseline loader is still restored and the
  real saves compared.
- `applyhandoff guest` (after `waitexit guest`): it checks
  `run\guest-saves-handoff\request.json` against the exited game's pid and start,
  plus its staged set (`handoff.json` names, sizes, hashes and the P2-6 set hash).
  It also records the copy's hashes. The supervisor holds `run\guest-saves` and
  its `287700\local` and `remote` folders without delete sharing until it exits,
  so the import runs after Stop has closed the session. It calls
  `Import-CoopProgressFiles` (`tools/friend/FriendCommon.psm1`) with
  `-SaveDir run\guest-saves`, `-ReplacedRoot run\guest-saves-replaced`,
  `-Suffix -before-handoff` and the `handoff` marker. `scenario.json`
  `HandoffApply` then holds the before and after hashes, the aside folder and
  the checks: progress equals the stage, settings are the guest's own, the
  aside holds the old progress, and the marker has source `handoff`.

Two-session restart proof (hidden checks R4 and R5): first run
`scenarios/handoff-restart.txt`, in which the guest stages, counts down, closes
its window, exits, and gets the staged set applied. Then run
`scenarios/handoff-after-restart.txt` with `-GuestSaveSeedRun <that run id>`.
Both titles must be 40010, the guest logs `need=0`, and both pair and follow
into 30010. `scenarios/handoff-stage.txt` is the transfer alone (R2, R3).
`scenarios/travel-lock.txt` is the guest lock and its escape (R6). Each header
gives the run command and the evidence. The worker has no "relaunch guest"
control, so a restart always takes two sessions.

## Primary API contracts and acceptance limits

- [Desktop connection precedence](https://learn.microsoft.com/en-us/windows/win32/winstation/thread-connection-to-a-desktop): inherited desktop handles take precedence over `lpDesktop`; the worker inherits none.
- [CreateDesktopW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createdesktopw) and [SetThreadDesktop](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setthreaddesktop): supervisor uses a fresh window-free thread and never displays the desktop.
- [Job UI restrictions](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_ui_restrictions): DESKTOP blocks CreateDesktop/SwitchDesktop; DISPLAYSETTINGS and SYSTEMPARAMETERS constrain shared changes.
- [Process creation attributes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute): child creation restriction has documented bypass limits outside a stronger sandbox.
- [GetAsyncKeyState](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate): inactive desktops cannot use that physical-key path; the reviewed virtual-input bridge is required.
- [DXGI fullscreen](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-setfullscreenstate) and [Present](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present): no focus/occlusion can prevent fullscreen/presentation; the probe stays windowed and reports exact results.
- [D3D11 creation](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdeviceandswapchain): the fixture requests hardware only, with no WARP fallback.
- [ClipCursor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-clipcursor): the cursor remains shared; MGSV requires separately reviewed early isolation hooks, not a desktop-isolation assumption.
- [SetErrorMode](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-seterrormode): process-local error mode suppresses OS error dialogs and is inherited by children.
- [CloseDesktop](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-closedesktop): restore only the supervisor helper thread's binding, then close owned desktop handles; borrowed GetThreadDesktop handles are never closed.

Observation polls are not a security boundary or a proof that no transient
external-broker UI could ever occur. They reject unknown ownership rather than
claiming to contain an unowned process. Qualify actual Steam startup and bridge
hook timing before admitting MGSV. If it cannot remain invisible and isolated,
stop and use a separately qualified Windows worker/VM; never show this desktop.

## Repair qualification receipts

The coordinator records each role's exact pre-run log offsets, including
`script-observer.log`, and writes the complete result to `scenario.json` after
cleanup. Inspect `Failure`, `ExpectFailures`, `IsolationFailed` and `Forced`,
plus the raw supervisor and cleanup receipts; a shell exit of zero alone is
not scenario acceptance. `Forced` includes both a controller-killed supervisor
and forced termination of its owned job. Resource failures such as Windows
commit-limit error 1455 fail closed and do not qualify gameplay.

`Invoke-WorkerScenario.ps1 -GraphicsQuality low` lowers only each freshly copied
role's render settings (six Low, SSAO Off, filtering Medium) before the existing
config hash pin. The default `preserve` keeps quality untouched. The pure helper
rejects invalid integrity tags, unsupported schema, missing/non-string quality
keys and duplicate JSON keys; unknown fields and other settings remain intact.
Real and existing lab graphics are read-only sources. This option addresses
resource pressure during two-copy tests, without changing other applications,
page-file settings, or admission and private-desktop checks.

`waitgameplay` uses the normal E disembark action only after an exact helicopter
guard result and fresh owned animation in a supported field mission. It stops
input when the on-foot position check succeeds. `host_continue` records both
pre-submission allocation generations; same-mission waits require a newer one
for each role. Submission and a previously ready scene cannot qualify restart.

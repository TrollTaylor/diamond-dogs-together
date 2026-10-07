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
only after successful child exit/admission, `qualification.txt`:

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

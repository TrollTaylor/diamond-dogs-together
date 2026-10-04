# Two-PC friend test package

The single-PC lab cannot form a session (see [October 3 testing](testing-2026-10-03.md)):
co-op needs the game's online login, and the local fuse server does not support
exe 1.0.15.4. The friend test therefore uses two PCs. Each player has their own
Steam account and logs into the game's normal online service as usual. The
master-server topic stays out of scope here. The friend package's `coop.ini`
never contains `master_url`.

**Status: prepared and fixture tested only.** No friend test has been run yet.
The scripts have never run on a second PC. Live co-op remains unverified.

## Building the package

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools/package.ps1 -Friend
```

This builds the loader with `tools/build.ps1 -BuildOnly` and stages
`dist/mgs-coop-friend-<version>-<stamp>/` plus a `.zip`. `-LoaderPath <dll>`
packages an existing loader without building. `-OutputRoot <folder>` writes
elsewhere. The tests use both options. Without `-Friend`, the script produces
the unchanged developer package. The only difference there is that source
copies now also include `.psm1` files such as `tools/lab/LabCommon.psm1`.

| Path in the package | Purpose |
|---|---|
| `Install-Coop.bat`, `Uninstall-Coop.bat`, `Collect-Logs.bat` | Double-click entry points. They clear `PSModulePath`, start `powershell.exe` (Windows PowerShell 5.1) with `-ExecutionPolicy Bypass`, pass the arguments through and pause at the end. |
| `FRIEND-GUIDE.txt` | Step-by-step guide for two non-developers (requirements, install, test sequence, logs, uninstall, limits and risks). |
| `RUNTIME-ARCHIVE.txt` | Where `mgs-coop-player-runtime.dat` comes from (see [below](#runtime-archive)). |
| `package.json`, `payload/` | The same manifest format as the developer package (`product=mgs-coop-local-package`, `flavor=friend-test`): loader, Dynamite/MinHook/tpp-mod licenses, notices. The runtime archive is referenced by SHA-256 only. |
| `scripts/` | `Install-Coop.ps1`, `Uninstall-Coop.ps1`, `Collect-Logs.ps1`, `FriendCommon.psm1`, `LabCommon.psm1` (save backup/seed, identity, manifests), `install.ps1` (= `tools/package-install.ps1`, owned install and rollback) and `set-session-mode.ps1` (the shared role command). All are ASCII-only with CRLF line endings, because Windows PowerShell 5.1 reads BOM-less scripts as ANSI. |
| `source/`, `source-files.json` | Current working source with hashes, as in the developer package (GPL source for the Dynamite-derived loader). |

The package contains no game-derived files. The tests check this by file
type, name, archive size and source paths.

## What Install-Coop does

Every question has a parameter: `-GameDirectory`, `-Role Host|Join`,
`-HostSteamId`, `-Character`, `-RuntimeArchive`, `-ReseedSaves`, `-Yes` and
`-NonInteractive`, plus `-SteamPath`/`-SteamAccountId` for fixtures.
`-NonInteractive` never prompts. A missing role, host ID or archive then
stops with an explanation; a missing character uses the role default.

1. **Game folder.** It searches the Steam roots from the registry,
   `steamapps/libraryfolders.vdf` (current and old format),
   `appmanifest_287700.acf` `installdir` and the `Steam App 287700` uninstall
   entry, then asks the player to confirm. If nothing is found, it asks for
   the path.
2. **Checks.** It verifies the `mgsvtpp.exe` and `steam_api64.dll` hashes
   from `package.json`. It refuses while `mgsvtpp`/`mgsvmgo` runs from that
   folder (matched by path; an unreadable path counts as running). It refuses
   links, and save copy paths longer than the native 213-byte `save_dir` limit.
3. **Steam account.** It uses Steam's `ActiveUser` from the registry.
   Without a logged-in user, it falls back to the only `userdata` account that
   has MGSV saves (LabCommon `Resolve-SteamAccount`). Own SteamID64 =
   76561197960265728 + account.
4. **Role, host ID and character.** For Join, the host's SteamID64 must have
   17 digits, start with `7656119` and be an individual account. The
   placeholder `76561198000000001` and the player's own ID are refused.
   Interactive input is asked again up to 5 times. Character defaults are
   Host=`default` and Join=`quiet` ([characters](characters.md)).
5. **Runtime archive.** Sources, in order: `-RuntimeArchive`, an installed copy
   with the right hash, a copy next to `Install-Coop.bat`, then a prompt. The
   hash is verified before anything is changed.
6. **Confirmation.** It shows a summary and asks once (skipped with `-Yes`).
7. **Save backup.** LabCommon `Backup-RealSaves` copies
   `userdata/<account>/287700` and `/311340` with a SHA-256 manifest to
   `<game>\mgs-coop\real-save-backups\<time>`. Real files are only read.
   `Copy-SaveSeed` then seeds `<game>\mgs-coop\coop-saves` from that backup.
   An existing copy is kept unless `-ReseedSaves` is given or the player says
   yes. A replaced copy is moved aside, never deleted.
8. **Install.** It copies any existing `lab.ini`, `coop.ini`,
   `script-observe.enabled` and `client-host.txt` aside as
   `*.before-friend-<time>` (first friend install only). It then runs
   `scripts\install.ps1 -Action Install -PackageDirectory <package> -NoPrompt`
   (owned install, hash-checked backups, its own partial-failure rollback), and
   `scripts\set-session-mode.ps1 -Role Host|Join [-HostSteamId]` (exact
   `host-v3`/`client-v1` markers and `client-host.txt`). It writes `lab.ini`
   and `coop.ini` (below) and `mgs-coop\friend-install.json` (role, character,
   backups, the rollback IDs this installer created, the previous files).
   If a step after the loader install fails, it puts the previous config
   files back. If this run created a new ownership record, it also runs
   `install.ps1 -Action Restore`.
9. **Done.** The host sees its own SteamID64 in a banner (also copied to the
   clipboard). Both players get the next steps.

### Diagnostic-mode configuration written

`lab.ini` (role `host`, or `guest` for Join):

```ini
# Generated by the MGS Co-op friend-test installer (Install-Coop.ps1).
# ... (explanation comments)
enabled=1
role=host
loopback=0
self_steamid=real
save_dir=<game>\mgs-coop\coop-saves
join_trigger=0
```

With `loopback=0` and `self_steamid=real`, `early_patches.cpp` installs no
networking gate and no identity override (`want_gate` is false), so Steam
networking and identity pass through. `save_dir` redirects the local save
folder and Steam remote storage to the copy. Lab status logging is on:
`lab-status.json`, `session_status=` lines, connect diagnostics and
`host-steamid.txt` on the host. The lab's **automatic client resets** are on
as well: session error, bad state, released session, or no
`client_established` within 60 s, at most 5 per process
(`ClientAutoResetWatch`). A reset while a live owned client session remains
is refused (`owned_session_active`).

`coop.ini`:

```ini
# Generated by the MGS Co-op friend-test installer (Install-Coop.ps1).
# ...
character=quiet
character_fallback=dd_male
```

The tests parse both files with the native parsers (`lab_ini_check`):
`present=1`, the expected role, `loopback=0`, `self_steamid=0` (real),
`join_trigger=0`, `xinput=-2` (pass-through), `master_url` empty and
`0/0` unknown/invalid keys.

## Uninstall-Coop

1. It finds the install (`-GameDirectory`, the package's
   `installed-location.json`, or a Steam folder with
   `mgs-coop\friend-install.json`) and refuses while the game runs.
2. It runs `install.ps1 -Action Restore` repeatedly, but only while the
   current ownership record's `rollback_id` is one that Install-Coop recorded.
   An MGS Co-op loader that existed before the friend test is therefore put
   back, not removed. If a restore fails, it stops and leaves `lab.ini` in
   place, so the game keeps using the save copy. Exit code 2.
3. It renames `lab.ini` and `coop.ini` to `*.disabled-<time>`. If no loader
   remains, the role marker and `client-host.txt` are renamed the same way. If
   an earlier loader remains, their pre-friend copies are put back. Earlier
   `lab.ini`/`coop.ini` copies are reported, not restored, because they may
   contain a lab `master_url`. `friend-install.json` becomes
   `friend-install.json.uninstalled-<time>`.
4. LabCommon `Test-RealSavesAgainstBackup` compares the real saves with the
   latest backup manifest. A difference is listed (exit code 4) with copy-back
   instructions. Real saves are never overwritten. Backups, the save copy and
   logs are kept.

## Collect-Logs

This writes `Desktop\mgs-coop-logs-<host|join|...>-<time>.zip`
(`-OutputDirectory` overrides the Desktop). The ZIP contains one folder with:

- `script-observer.log` from the last `MGS Co-op script adapter` start line;
- the tails of `probe.log`, `steam-observer.log` and `early.log`;
- `lab-status.json`, `lab.ini`, `coop.ini`, `script-observe.enabled`,
  `probe-install.json`, `client-host.txt`, `host-steamid.txt` and
  `friend-install.json`;
- `summary.txt`: role, versions, the SHA-256 of the exe, Steam DLL, loader
  and archive (each with an expected/matches flag), the real-save check
  result, the Windows and PowerShell versions, and the included and missing
  files.

Files are read with `FileShare.ReadWrite|Delete`, so the game may still be
running. Each log is capped (`-MaxLogBytes`, default 1 MiB; the probe and
Steam tails 256 KiB, early 64 KiB, small files 64 KiB).

Redaction (`Hide-FriendSensitiveText`) works as follows:

- Every 17-digit SteamID64 (prefix `7656119`, or any individual ID) becomes
  `7656119XXXXXX` plus its last 4 digits.
- `userdata\<number>` becomes `userdata\<account>`.
- `X:\Users\<name>` becomes `X:\Users\<user>`.

## In-game feedback

Messages use `TppUiCommand.AnnounceLogView` in Mother Base (30050) only.
Status messages come from `session_feedback.lua`: they are polled every 60
updates while a host session exists or a guest request is pending, and appear
only on change.

| Mode | Situation | Message |
|---|---|---|
| Join | Mother Base loaded, guest prepared, join armed | `Co-op: wait for the host to be ready, then press Ctrl+F8 to join.` |
| Join | Armed check blocked at mission start | `Co-op: guest setup is unavailable. Restart the game before trying again.` |
| Join | Ctrl+F8, native request submitted | `Co-op: join request sent. Waiting for connection.` |
| Join | Ctrl+F8, refused or the request did not start | `Co-op: join could not start. Press Ctrl+F9, then Ctrl+F8 to retry.` **(changed)** |
| Join | Ctrl+F9 (or an automatic lab reset) re-armed the join | `Co-op: join reset. When the host is ready, press Ctrl+F8 to join.` **(changed)** |
| Join | Session state 7 with two members | `Co-op: host connection established.` |
| Join | Still connecting after 30 observations, now **once per attempt** | `Co-op: still connecting. Ctrl+F9 resets; restart the game if it does not.` **(changed)** |
| Join | Session error flag | `Co-op: session error. Press Ctrl+F9 to reset, or restart the game.` **(new guest variant)** |
| Host | Host session state 3, started, accepting | `Co-op: host is accepting connections. The guest can press Ctrl+F8.` |
| Host | Session exists, not accepting | `Co-op: host session exists, but is not accepting connections.` |
| Host | A second member is present | `Co-op: a guest is present in the session.` |
| Host | Session error flag | `Co-op: the game reported a session error. Restart before another attempt.` |
| Both | Status reader cannot confirm the state | `Co-op: session status is currently unavailable.` |
| Both | Respawn (`respawn.lua`) | `Respawn in 10 seconds`, `Respawning` |

Changes in this work package (Lua only):

- `session_feedback.lua` now resets its per-attempt state (last status,
  pending counter, reminder) whenever no guest request is pending. Before
  this, after a failed attempt and a Ctrl+F9 re-arm, a repeated status (for
  example the same error) and the pending reminder were silently suppressed
  on the retry.
- Guests get reset/restart guidance for errors and long pending connections.
- The lab trigger-file jargon was removed from player-facing text.

All messages fit 80 characters. They are covered by the new
`session_feedback_lua_check` and extended `client_join_lua_check`.

Remaining gaps. Each needs a native signal, which is outside the two Lua files
this package may change:

- **No message when the host session cannot be created** (for example
  offline, `dynamite_host=returned; result=0`). `host_session_trial.lua`
  discards the creation result, and the status receipt is not answered once
  the rules are restored. The host sees nothing. FRIEND-GUIDE.txt tells the
  host what to do if no co-op message appears within about a minute.
- **No message when guest preparation fails during loading.** The creation
  receipt returns false, `clientPrepared` stays unset and `client_join.lua`
  never arms.
- **No message for a refused Ctrl+F9 reset** (`owned_session_active`,
  `nothing_to_reset`, `not_ready`, `auto_reset_limit`). These are only logged.
  The guide and the messages therefore say "restart the game if no reset
  message appears".

## Runtime archive

`mgs-coop-player-runtime.dat` (SHA-256 `26CE7C21…382F54`, 3,720,192 bytes)
holds game-derived bytes and is never bundled.

The existing generator chain (local, developer-only) needs these game inputs,
read with `build/archive_probe.exe` from `master/0/00.dat` and
`chunk0`–`chunk4.dat`:

- `two_players_game_obj.fpkd` and its decoded FOX2 XML;
- `mtbs_mission_fob.fpk`;
- `player2_location_motion.fpk` and `player2_location_vram_motion.fpk`
  (hash-checked);
- `f30050.fpkd` and `f30050.fpk` (from `chunk3.dat`).

It also needs four pinned Dynamite `moddata` XML files: the controller and
three supplemental definitions from `work/upstream-dynamite`. The Python stages
then run in this order:

1. `stage_four_player_assets.py`
2. `compile_player_asset_stage.py`
3. `stage_player_runtime_bundle.py --additional-parts-count 1` (output `28308ddb…`)
4. `stage_two_player_control.py` (output `c6603a58…`)
5. `stage_controller_replacement.py --players 2` (output `26CE7C21…`)

Every stage pins its inputs by SHA-256 and calls `datfpk.exe` (`a1ec95c8…`).

Evidence gathered on October 3: re-running stages 4 and 5 on this PC
(Python 3.14.3, from the existing intermediate inputs) reproduced
`c6603a58…` and `26CE7C21…` byte for byte. The outputs were deleted
afterwards. Stages 1–3 were not re-run.

**A self-contained, double-click generator for the friend's PC is not
feasible right now, and none was implemented.** The reasons:

1. `datfpk` and `archive_probe` (which embeds datfpk's parser) have no license
   file in the pinned v0.3.0 checkout (`tools/archive_probe/README.md`:
   distribution rights unresolved). Their binaries cannot be shipped. The
   friend would have to build them with Go or obtain them separately.
2. The stages need Python 3.9 or newer (`Path.is_relative_to`,
   `str.removesuffix`, `ET.indent`). That is not a reasonable requirement for
   a non-developer.
3. The stages write only under the repository's `build/` and read pinned
   inputs from `work/`. They would need restructuring into one standalone
   run.

The installer therefore accepts only an archive that already exists, verified
by its SHA-256. The friend must obtain the archive generated from their own
game install with the developer procedure above. Alternatively, the host may
share theirs: the installer accepts only that one hash. Sharing a game-derived
file is the user's decision; the package does not do it.

## Tests

`tests/friend_package_tests.ps1` is registered in CTest as
`friend_package_tests` with `-IniCheck` and `-FakeProcess`. All its work is
inside `build/friend-package-tests-<guid>`. The friend scripts run as child
processes under Windows PowerShell 5.1 (5.1.26100 on this PC); one Join
install runs under PowerShell 7. The suite covers:

- SteamID validation, including the placeholder, own ID, prefix, length and
  non-individual IDs;
- lab.ini and coop.ini text for both roles, against the native parser, with
  no `master_url`;
- save-dir limits, redaction, log tails and markers;
- `libraryfolders.vdf`/appmanifest discovery;
- the staged package's contents and ZIP, ASCII and CRLF checks, the absence
  of game files, and the guide's quoted messages matching the Lua sources;
- refusals (placeholder/own/short ID, character, archive hash, modified exe or
  Steam DLL, no Steam account, missing non-interactive input), each leaving
  the folder untouched;
- Host install (packaged layout), reinstall, Join install, and rollback after
  a forced failure;
- Collect-Logs, both directly and through `Collect-Logs.bat`;
- refusal while a stand-in `mgsvtpp.exe` runs (`lab_fake_process`);
- Uninstall after two installs, a changed-save report with no overwrite, and
  Uninstall back to an earlier owned loader with its role marker.

The staged role command's pinned archive hash is replaced by the fixture
archive hash; that is its only change. The fixture Steam saves keep their
hashes across the whole run.

What the tests cannot cover:

- the interactive prompts (`Read-Host`), registry-based Steam discovery and
  the clipboard;
- a real Steam installation, a real game folder and a second PC.

These remain unverified until the first friend test.

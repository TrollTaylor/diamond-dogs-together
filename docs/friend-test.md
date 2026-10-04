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
`-HostSteamId`, `-Character`, `-RuntimeArchive`, `-ReseedSaves`,
`-AcceptChangedSaves`, `-Yes` and `-NonInteractive`, plus
`-SteamPath`/`-SteamAccountId` for fixtures.
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
   links, save copy paths longer than the native 213-byte `save_dir` limit,
   and game folders whose backup and installer paths would exceed 259
   characters (`Get-FriendPathLengthProblem`; Windows PowerShell 5.1 cannot
   go past that on a default Windows setup without long paths enabled).
   All JSON records are read as UTF-8, so non-ASCII folder names work under
   Windows PowerShell 5.1 too.
3. **Steam account.** It uses Steam's `ActiveUser` from the registry.
   Without a logged-in user, it falls back to the only `userdata` account that
   has MGSV saves (LabCommon `Resolve-SteamAccount`). Own SteamID64 =
   76561197960265728 + account.
   **Reinstall check.** With an active `friend-install.json`, the real saves
   are first compared with the first backup of each Steam account (the state
   before the friend test). If they changed, it lists the differences and
   asks (default No); `-NonInteractive` refuses unless `-AcceptChangedSaves`
   is given. A reinstall never silently makes a new reference.
4. **Role, host ID and character.** For Join, the host's SteamID64 must have
   17 digits and be an individual account (76561197960265729 to
   76561202255233023, the same rule as the native parser and
   `set-session-mode.ps1`). Most IDs start with `7656119`; the newest accounts
   start with `7656120`. The placeholder `76561198000000001`, the single-PC
   lab's stand-in ID `76561202255233008` and the player's own ID are refused.
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
   Without an active friend install (first install, or again after
   Uninstall-Coop), a copy left from an earlier test is always replaced. On a
   reinstall the copy is kept unless `-ReseedSaves` is given or the player says
   yes (the prompt shows when the copy last changed). A replaced copy is moved
   to `coop-saves-replaced\<time>`, never deleted.
8. **Install.** It copies any existing `lab.ini`, `coop.ini`,
   `script-observe.enabled` and `client-host.txt` aside as
   `*.before-friend-<time>` (first friend install only). The order is chosen
   so that the loader never runs without the save redirection and every
   stopped run can be undone:
   1. `mgs-coop\friend-install.json` with `status=pending` and
      `pending_record_before` (the ownership record before this run);
   2. `lab.ini` and `coop.ini` (below), inert without the loader;
   3. `scripts\install.ps1 -Action Install -PackageDirectory <package> -NoPrompt`
      (owned install, hash-checked backups, its own partial-failure rollback);
   4. `scripts\set-session-mode.ps1 -Role Host|Join [-HostSteamId]` (exact
      `host-v3`/`client-v1` markers and `client-host.txt`);
   5. `friend-install.json` with `status=installed` (role, character, backups,
      the rollback IDs this installer created, `record_before_friend`, the
      previous files).

   If a step fails and this run created a new ownership record, it runs
   `install.ps1 -Action Restore`. Only when the loader and its record are then
   exactly as before the run are the configuration files and
   `friend-install.json` put back. Otherwise `lab.ini` and the pending record
   stay, the player is told to run Uninstall-Coop, and Uninstall-Coop (or the
   next Install-Coop run) treats a newer ownership record than
   `pending_record_before` as part of the friend test. The same applies when
   the window is closed or the process dies halfway.
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
   current ownership record's `rollback_id` is one that Install-Coop recorded
   (with `status=pending`, also a newer record than `pending_record_before`).
   An MGS Co-op loader that existed before the friend test is therefore put
   back, not removed. If a restore fails, it stops and leaves `lab.ini` in
   place, so the game keeps using the save copy. Exit code 2.
3. It renames `lab.ini` and `coop.ini` to `*.disabled-<time>`. If no loader
   remains, the role marker and `client-host.txt` are renamed the same way and
   earlier `lab.ini`/`coop.ini` copies are only reported. If a loader remains,
   the pre-friend copies of all four files are put back (for the single-PC
   lab, its `lab.ini` keeps that loader on its save copy; an earlier
   `coop.ini` comes back as it was, including a lab `master_url`).
   `friend-install.json` becomes `friend-install.json.uninstalled-<time>`.
   A remaining loader is named exactly: the one from before the friend test
   (its `rollback_id` equals `record_before_friend`), or one the friend test
   did not install (for example the developer package put on top). Either
   way the output says that it stays active and whether a `lab.ini` save_dir
   redirects its saves or it uses the real saves.
4. LabCommon `Test-RealSavesAgainstBackup` compares the real saves with the
   FIRST backup of each Steam account (`Get-FriendSaveBaselines`), not the
   latest one, so reinstalls do not move the reference. A difference is listed
   (exit code 4) with copy-back instructions for exactly
   `files\287700\local` and `files\311340\remote` of that backup (never
   `remotecache.vdf`, Steam Cloud's bookkeeping). Real saves are never
   overwritten. Backups, the save copy and logs are kept.

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
  The messages and the guide therefore tell the player to restart the game
  when Ctrl+F9 shows no reset message.

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

- SteamID validation, including the placeholder, the lab stand-in ID, own
  ID, the `7656120` range, length and non-individual IDs;
- lab.ini and coop.ini text for both roles, against the native parser, with
  no `master_url`;
- save-dir and path-length limits, redaction, log tails and markers;
- `libraryfolders.vdf`/appmanifest discovery;
- the staged package's contents and ZIP, ASCII and CRLF checks, the absence
  of game files, and the guide's quoted messages matching the Lua sources;
- refusals (placeholder/own/short ID, character, archive hash, modified exe or
  Steam DLL, no Steam account, missing non-interactive input), each leaving
  the folder untouched;
- Host install (packaged layout), reinstall, Join install;
- failures, using a staged copy with a stand-in role command: a failure with
  a working rollback (everything put back), a failure whose rollback also
  fails (loader kept with `lab.ini` and a pending record; Uninstall-Coop
  first stops with exit 2, then succeeds once the cause is gone), and a
  process that dies halfway (pending record; Install-Coop adopts it,
  Uninstall-Coop undoes both runs); a `lab.ini` write failure before the
  loader is touched;
- a complete Host install, reinstall, Collect-Logs and uninstall (through
  `Uninstall-Coop.bat`) in a game folder, Steam folder and package folder
  with non-ASCII names, under Windows PowerShell 5.1;
- a reinstall after the real saves changed (refused without
  `-AcceptChangedSaves`; uninstall compares with the first backup and prints
  the folder-level copy-back steps);
- a reinstall after an uninstall (old save copy moved aside, fresh copy);
- a game folder whose installer paths would pass 259 characters (refused,
  untouched);
- Collect-Logs, both directly and through `Collect-Logs.bat`;
- refusal while a stand-in `mgsvtpp.exe` runs (`lab_fake_process`);
- Uninstall after two installs, a changed-save report with no overwrite,
  Uninstall back to an earlier owned loader with its role marker, `lab.ini`
  and `coop.ini`, and Uninstall with a developer-package loader installed on
  top of the friend install (named as not installed by the friend test).

The staged role command's pinned archive hash is replaced by the fixture
archive hash; that is its only change. The fixture Steam saves keep their
hashes across the whole run.

What the tests cannot cover:

- the interactive prompts (`Read-Host`), registry-based Steam discovery and
  the clipboard;
- a real Steam installation, a real game folder and a second PC.

These remain unverified until the first friend test.

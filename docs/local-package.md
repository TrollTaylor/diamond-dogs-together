# Local development package

The current package is **0.10.33**, including natural session release, loading-worker lifecycle fixes and a manual Diagnostic check. The September 12 packaging code addresses concrete issues in the user's 0.10.27 distribution. Its batch configurator wrote into the extracted package, emitted markers with trailing spaces/CRLF and expanded the host ID before reading it. Its installer could overwrite an unrelated loader and replace its fixed backup.

`tools/package.ps1` compiles only the loader target and creates a new timestamped folder and ZIP under `dist`. Pass `-RuntimeArchive` with a local archive whose SHA-256 is `26CE7C2168D655C35BE39E4F17A295206AA0522DE8782CD57A36C4FEEA382F54`, matching the loader's existing archive gate. The package includes the current working source, a source hash inventory, and the Dynamite, tpp-mod and MinHook licenses. It does not reuse the old installer or installation manifest.

The new package contains `install.bat`, `install.ps1`, `configure-role.bat`, `configure-role.ps1`, `set-session-mode.ps1`, a payload manifest and the payload directory. The batch files only invoke PowerShell. They do not interpolate a host ID into a command or write marker files themselves.

Installation requires the game to be closed and checks the exact MGSV executable and shipped Steam DLL hashes. An existing loader must match this mod's ownership record. Only the enumerated loader/mod files can be replaced; link paths and path traversal are refused. Each target is backed up under a unique `mgs-coop/install-backups/<id>` folder. Payload and backup hashes are checked, individual replacements are staged, and partial installation failures attempt rollback only while the written targets still match their recorded hashes.

`install.ps1 -Action Restore -GameDirectory <folder>` uses the current installed rollback ID. It checks the complete managed inventory and backups before restoring the previous loader/files, or removing files created by that installation. Modified managed files cause refusal. Role files, logs and backup history are retained. A game launched during an update, locked files or failed storage operations can still require manual recovery from the recorded backup; the successful fixture restores do not establish recovery from every operating-system or storage failure.

After installation, `configure-role.bat` reads the recorded install location or asks for the game folder. It delegates Host/Join/Diagnostic selection to the shared role command, which verifies installed ownership, DLL/archive hashes and SteamID64 format, and writes exact ASCII/LF markers. The first install defaults to loading diagnostics; an upgrade retains the current role.

In 0.10.31 and later, Diagnostic mode waits for Ctrl+F8 after Mother Base loads.
It uses the same foreground-window and key polling path as Join mode, then checks
native prerequisites without submitting a connection. One attempt is allowed
per game process. A successful diagnostic is distinct from an established peer.

This workflow prepares a local development artifact. It is not evidence of a working connection, remote actor or shared combat. September 12 tests passed fresh install/restore, owned upgrade/restore, unrelated-loader refusal and changed-loader restore refusal in a disposable game-directory fixture. They exposed and fixed an empty backup-path conversion in PowerShell `File.Replace` by passing `[NullString]::Value`. The corrected installer then performed owned live upgrades through 0.10.33. That build completed Host and Diagnostic lifecycle checks and quit normally; Host mode remains selected. The original executable, Steam DLL and all seven save/config files match the prelaunch baseline. See [test results](testing-2026-09-12.md).

The accompanying source review also confirmed that the pinned DYNAMITE custom schema contains marker, variable, emblem and Boss Quiet messages. Its `TppGmPlayerImplSynchronizerImplInitializeHook` forwards the original initializer. There is no basis in that schema for substituting this project's standalone transform codec for the game's player networking. The remaining climbing and path-result mappings are still unresolved; no candidate-only hook was added.

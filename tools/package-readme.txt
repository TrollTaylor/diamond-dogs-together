MGS Co-op 0.12.3 - native-session diagnostic development package

This package retains the older native-session diagnostic workflow below. Use
the friend package and Play-Coop.bat for the current two-player play workflow.
It contains the current loader, corresponding source and license notices. The
runtime archive must already be installed with the expected hash or supplied
locally from your own licensed game. See RUNTIME-ARCHIVE.txt. The archive,
retail executable, shipped Steam DLL and saves are not included in this ZIP.

1. Close MGSV normally. Extract this ZIP to a folder.
2. Run install.bat and enter the MGSV folder containing mgsvtpp.exe.
   An existing MGS Co-op loader must match its ownership record. An unrelated
   loader is refused. The installer saves the previous managed files for rollback.
3. Run configure-role.bat. Choose Host, Join or Diagnostic loading. Join asks for
   the HOST player's 17-digit SteamID64. It configures the installed mod.
4. Both players load Mother Base. The host waits for the accepting-connections
   message, then the guest presses Ctrl+F8 in MGSV. One attempt per game restart.

Diagnostic loading prepares the package. Press Ctrl+F8 in Mother Base to check
the guest input and native prerequisites without requesting a peer connection.
It does not spawn a fake player. Installer recovery fixtures and a live host
handoff from a loading worker to gameplay passed. Live remote player control
and the physical Ctrl+F8 shortcut remain unverified.

Command-line examples from the extracted package folder:
  powershell -NoProfile -File .\install.ps1 -GameDirectory "D:\Games\steamapps\common\MGS_TPP"
  powershell -NoProfile -File .\configure-role.ps1 -Role Host
  powershell -NoProfile -File .\configure-role.ps1 -Role Join -HostSteamId HOST_ID_HERE

To restore the previous loader and managed mod files, close MGSV, then run:
  powershell -NoProfile -File .\install.ps1 -Action Restore -GameDirectory "D:\Games\steamapps\common\MGS_TPP"
Restore checks current files and backup hashes before changing anything. Edited
managed files cause refusal. Role configuration, logs and backups are retained.

The source folder includes the current working edits. THIRD_PARTY_NOTICES.md and
the retained licenses identify Dynamite (GPLv3 + Commons Clause 1.0), tpp-mod and
MinHook adaptations/dependencies. This is a private local development package.

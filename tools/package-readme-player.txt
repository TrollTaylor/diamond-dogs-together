Diamond Dogs Together @VERSION@
Two-player co-op for METAL GEAR SOLID V: THE PHANTOM PAIN.
A fan project, not affiliated with Konami.

WHAT YOU NEED
- MGSV: The Phantom Pain from Steam, game version 1.0.15.4, with no other
  mods. Diamond Dogs Together stops if the game files are not the originals.
- Steam running, each of you logged in to your own account (online).
- The same Diamond Dogs Together version on both PCs.
- A save that can Continue into Mother Base or free roam.
- Windows 10 or 11.

INSTALL (drag and drop)
1. Quit MGSV.
2. Open the game folder: in Steam, right-click the game, then
   Manage > Browse local files. It is the folder with mgsvtpp.exe.
3. Copy everything from this zip into that folder: DiamondDogsTogether.exe,
   dinput8.dll and the mgs-coop folder. Replace the files if Windows asks.
4. Start DiamondDogsTogether.exe from the game folder.
5. The first start gets co-op ready by itself (about a minute, nothing to
   click): it checks the game files, backs up your real saves (they are
   only read), makes a co-op copy of your save, and builds
   mgs-coop\mgs-coop-player-runtime.dat from your own game files.
   Nothing is downloaded. If that fails, Play says why and has Try again.

Starting MGSV from Steam on its own is still your normal game on your real
saves. Co-op is on only for a game started from Diamond Dogs Together.

PLAY
- Host: open Play, choose Host and press "Host game". You play as Snake.
  A join code is copied to the clipboard: send it to your partner (Steam
  chat, Discord), or invite them from the Steam overlay (Shift+Tab) once
  you are in the game. A new code is made every time you host.
- Join: open Play, choose Join, pick your character, paste the code and
  press "Join game". Or tick "Wait for a Steam invite instead" and accept
  the host's invite in the Steam overlay.
- Both: choose Continue at the title screen and meet in Mother Base or in
  Afghanistan or Africa free roam. "Failed to log in" on the title screen
  is expected: the game stays offline from Konami's servers.
- Leave Diamond Dogs Together open while you play: it shows the connection.
  If you close it, a hidden helper switches co-op off after the game exits.
- Quit MGSV normally when you are done.

SAME POINT IN THE STORY
Co-op pairs when both games are in the same Mother Base, free roam or
helicopter. If the joiner's save starts somewhere else, the host's save is
copied across at the title screen and MGSV restarts once on it (about a
minute). Your own co-op copy is kept: Tools > Share save > Restore previous
brings it back. To share by hand: quit the game, Tools > Share save >
Export puts a zip on your Desktop; your partner uses Import... with it.
Your real Steam saves are never part of this.

TROUBLESHOOTING
- "mgsvtpp.exe is not the expected Steam version" or a steam_api64.dll
  message: verify the game files in Steam (Properties > Installed Files).
- "dinput8.dll ... is not the one from this package", or "different
  versions": copy the whole zip into the game folder again. Another mod
  that uses dinput8.dll cannot run at the same time.
- Not connecting: both PCs must be online in Steam, on the same version,
  and the joiner needs the host's current code. The In game page and the
  CO-OP line in the game say why.
- Different missions: meet in the same Mother Base, free roam or
  helicopter. A save still in the hospital opening always Continues into a
  story mission, and co-op cannot join that: share a save first.
- To report a problem: quit the game, Tools > Collect logs, and send the
  zip from your Desktop. Join codes, the link key and your Windows user
  name are removed; Steam IDs are cut down to the last four digits.

UNINSTALL
1. Quit MGSV.
2. Start DiamondDogsTogether.exe, open Tools and press Uninstall. It
   switches co-op off, removes dinput8.dll (only when it is this
   package's), sets the co-op settings aside and checks your real saves
   against the first backup. It never overwrites a save.
3. Delete DiamondDogsTogether.exe and the mgs-coop folder yourself if you
   want them gone. The mgs-coop folder holds the backup of your real saves,
   the co-op save copy and the logs: keep it until you no longer need them.

LICENCES AND SOURCE
mgs-coop\THIRD_PARTY_NOTICES.md lists the third-party code: Dynamite
(GPLv3 with the Commons Clause), tpp-mod (GPLv3), MinHook (BSD 2-clause)
and Dear ImGui (MIT); the full licence texts are next to it. The complete
source code of the two programs in this zip is DDT-source-@VERSION@.zip:
download it from the same release page as this zip (release v@VERSION@ on
https://github.com/TrollTaylor/diamond-dogs-together/releases). It was
made from the commit of that repository named on the "source_commit" line
of mgs-coop\package.json ("source_dirty": false means the files did not
differ from that commit). The zip has MinHook's source files, where the
repository has only a link to MinHook's own repository, and it leaves out
the repository files the programs are not built from: README pictures, git
settings, developer notes and launch scripts, and evidence files. The tag
v@VERSION@ can be on a later commit: at least the record of this package
in docs/STATUS.md is written after the package is built.

NETWORK
DiamondDogsTogether.exe goes online only to check this repository's
GitHub releases for a newer version (when the window opens, and when you
press Check for updates on Tools) and, when you press Update now, to
download that release's DiamondDogsTogether zip. The setup downloads
nothing. Co-op play itself goes through Steam.

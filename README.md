<p align="center">
  <img src="docs/readme/logo.png" alt="Diamond Dogs Together" width="280">
</p>

<p align="center">
  <b>Two-player online co-op for Metal Gear Solid V: The Phantom Pain</b><br>
  Windows · Steam · version 0.14.4 (development build)
</p>

<p align="center">
  <img src="docs/readme/afghanistan-together.jpg" alt="Two players in Afghanistan free roam; the partner's name floats above them." width="85%">
</p>

You and a friend each run your own copy of MGSV and play in the same world. You see each other move, fight, climb, drive and hide.

**What you can play today:** Mother Base and the Afghanistan and Africa free roam. Story missions, the hospital prologue, side ops and cutscenes are **not** co-op yet; see the [campaign roadmap](docs/CAMPAIGN-ROADMAP.md).

## Download

Get `DiamondDogsTogether-<version>.zip` from [Releases](https://github.com/TrollTaylor/diamond-dogs-together/releases). Both players need the **same version**.

The launcher updates itself: when a new version is out, **Update now** downloads it, installs it and restarts Diamond Dogs Together.

The complete source code of each version from 0.14.4 on is `DDT-source-<version>.zip` on the same release page; older packages carry it as `mgs-coop/source.zip`. It is made from the commit of this repository that `mgs-coop/package.json` names (`build.source_commit`), with MinHook's source in place of its submodule link and without the files the programs are not built from (README pictures, git settings, developer notes and scripts, evidence files; [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists them). The tag `v<version>` can sit on a later commit: at least the record of that package in `docs/STATUS.md` is written after it is built. You do not need it to play. The launcher's only network access is that update check against this repository's releases and, on **Update now**, the download of the new zip.

## Requirements

- MGSV: The Phantom Pain on Steam, version 1.0.15.4, with **no other mods**.
- Windows 10 or 11, and Steam running and logged in.
- A save that can **Continue** into Mother Base or free roam (not one still in the hospital opening).

## Install

1. In Steam, right-click MGSV and choose **Manage → Browse local files**. This opens the game folder, the one with `mgsvtpp.exe`.
2. Extract **everything** from the zip into that folder: `DiamondDogsTogether.exe`, `dinput8.dll` and the `mgs-coop` folder.
3. Run **DiamondDogsTogether.exe**.

The first start gets co-op ready by itself, in about a minute (the Play page shows *Getting co-op ready...*; there is nothing to click):
- It checks your game files.
- It backs up your real saves.
- It makes a co-op copy of your save.
- It builds the mod's runtime file from your own game files.

Launching MGSV normally from Steam still plays your real save, with the mod inactive.

## Play

1. **Host:** open **Play → Host → Host game**. MGSV starts and a join code is copied to your clipboard. Send it to your friend, or invite them from the Steam overlay once you are in the game. The host always plays Snake.
2. **Join:** open **Play → Join**, pick your character, paste the code and press **Join game**. Or tick **Wait for a Steam invite**. Character choices:
   - **Story: Quiet or Ocelot.** Quiet while she is on your team, Ocelot otherwise.
   - **Ocelot.**
   - **Diamond Dogs soldier,** male or female.
   - **Your custom character.**
3. **Both:** choose **Continue** and meet up in Mother Base or free roam. If your saves are at different points, the host's save is copied to the joiner automatically and their game restarts once.

Keep the launcher open while you play; its **In game** page shows the connection and your partner's status. Quit MGSV normally when you are done.

## What works

- **Your partner's body:** you see their body, gear and weapons as they walk, run, crouch, crawl, aim, reload and throw.
- **Movement:** ladders, ledges, the cardboard box and item pickups.
- **Vehicles:** your partner drives; you can ride in the back seat of a jeep. In Afghanistan and Africa free roam, a world vehicle that nobody drives stays where it was left on both screens and has the body damage the host's PC gives it; either player's Fulton of it removes it on both screens and adds it to both players' Mother Base, one destroyed on the host's PC disappears from the partner's screen, and one the guest blows up near them is destroyed on the host's screen too.
- **Helping each other:** revive a downed partner, wake a sleeping one, and carry them.
- **Shared world:** enemy kills, sleeps and Fultons, alerts, time and weather.
- **Shared progress:** the host's travel, checkpoints, and game over with continue for both players.
- **Online play:** through Steam, with no port forwarding.

## Known limits

- Story missions, the hospital prologue, side ops and cutscenes are not co-op yet.
- **Enemies:** each PC still moves its own enemies. Only what happens to them (killed, asleep, Fultoned, alerted) is shared.
- **Vehicles:** trucks that enemies drive move separately on each PC, and so does an enemy vehicle that drives off in a fight: one the guest destroys after it has moved stays destroyed only on the guest's screen. Wheel damage and fire are not shared, and damage the guest does to a vehicle without destroying it is undone within about 10 seconds. A vehicle the guest destroys from more than about 300 m away (or while the host sits in it on the host's PC) stays destroyed only on the guest's screen. A vehicle the guest destroys is destroyed on the host's PC with the game's own explosion there, soldiers inside included, unless either player stands near the host's copy (within 15 m of a jeep, 50 m of a truck, armoured vehicle or tank): then a named one just disappears on the host's PC, and others (one with soldiers inside, or an unnamed one) wait until both players are clear, for up to 2 minutes; after that the vehicle stays destroyed only on the guest's screen. While D-Dog still sits in the host's copy, the host waits up to 2 minutes for the seat to empty. A vehicle destroyed on the host's PC leaves no wreck on the guest's screen. In Mother Base a parked vehicle's position, damage, Fulton and destruction are not shared yet.
- **Animals and prisoners** are not shared.
- **Buddies** (D-Dog, D-Horse, Quiet, D-Walker) are not shared yet: each PC keeps its own.
- **Seats:** you can ride along in a jeep's back seats. The jeep's front passenger seat and a truck's cab seat work in testing but are off in this version (with D-Dog along, the dog already sits in the jeep's front seat on the driver's screen); a truck's bed and the inside of armoured vehicles and tanks aren't available yet. If you both get into the same empty vehicle's driver's seat at the same moment, each of you ends up driving your own copy of it.
- **Sortie vehicles:** a vehicle you bring from sortie prep (the helicopter's vehicle choice) doesn't appear on your partner's screen; they see you without it. Use world vehicles you find on the map to ride together.
- **Supply drops:** only the host can order a LOADOUT or VEHICLES supply drop in co-op (both can bring a vehicle), also while the host's game is still loading after you travel together; the guest's iDroid says so.
- **Mounts:** a partner riding D-Horse or D-Walker is hidden. Shooting from a vehicle is not supported yet: in a jeep's back seat you can aim and fire, but your shots do not seem to hit, you can only aim across the jeep, your aim jumps up when you fire, the camera can swing to the sky or show the inside of the jeep, and your partner does not see you aim.
- **Look:** your custom character may look like Snake on your partner's screen.
- **Players:** two only.

## Troubleshooting

- **Won't connect:** both of you need the same version and Steam online, and the game must be started from the launcher. The launcher's **In game** page and the in-game **CO-OP** line say what is wrong.
- **Different missions:** you only pair in the same Mother Base, free roam or helicopter. If one save always continues into a story mission, use **Tools → Share save** first.
- **Red "Co-op did NOT start" banner:** you are playing normally on your real save. Quit and start the game from the launcher.
- **Reporting a bug:** quit the game, use **Tools → Collect logs** on both PCs, and send the zips it puts on the Desktop. Codes and personal IDs are removed.

## Uninstall

1. Run the launcher and use **Tools → Uninstall**. This turns co-op off, removes `dinput8.dll` and checks your real saves against the backup.
2. Then delete `DiamondDogsTogether.exe`, and the `mgs-coop` folder once you no longer need its backups.

## Building from source

You need CMake, the Visual Studio C++ build tools (x64 Windows SDK) and PowerShell.

```powershell
git clone --recurse-submodules https://github.com/TrollTaylor/diamond-dogs-together.git
cd diamond-dogs-together
pwsh -File tools/Prepare-Dependencies.ps1
./tools/build.ps1 -Configuration Release
```

`tools/package.ps1 -Friend` builds the release from a passing `tools/worker/Invoke-WorkerChecks.ps1 -Profile Release` run: the player zip `DiamondDogsTogether-<version>.zip` and the source zip `DDT-source-<version>.zip`. Every release uploads **both** to the GitHub release `v<version>` (see AGENTS.md, "Releasing").

Testing on one PC: in the launcher, **Settings → Debug mode** adds **Play → Local test (two windows)**. It starts two MGSV copies on this PC (this game folder hosts as Snake, a second game folder joins as Quiet) on separate local-test saves, and puts the test settings in both folders back after both games exit. Choose the second folder under Settings → Debug. The test copies this version's loader, runtime file and `package.json` into the second folder (an older Diamond Dogs Together launcher there is updated too) and they stay there; the files it replaces are kept in that folder's `mgs-coop\install-backups\local-test-<time>`. Its `probe-install.json` is never changed, and a loader that only the developer tools own is not replaced (update it with `tools\package-install.ps1` first). `Play-Offline-Coop.bat` (`tools/dev/Play-LocalCoop.ps1`) does the same from PowerShell; both use the same cleanup journal.

Developer docs:
- [Status and change log](docs/STATUS.md)
- [Campaign roadmap](docs/CAMPAIGN-ROADMAP.md)
- [Partner body](docs/native-puppet.md)
- [World sync](docs/world-sync-protocol.md)
- [Two-PC play](docs/two-pc-play.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

<p align="center">
  <img src="docs/readme/logo.png" alt="Diamond Dogs Together" width="280">
</p>

<p align="center">
  <b>Two-player online co-op for Metal Gear Solid V: The Phantom Pain</b><br>
  Windows · Steam · version 0.14.3 (development build)
</p>

<p align="center">
  <img src="docs/readme/afghanistan-together.jpg" alt="Two players in Afghanistan free roam; the partner's name floats above them." width="85%">
</p>

You and a friend each run your own copy of MGSV and play in the same world. You see each other move, fight, climb, drive and hide.

**What you can play today:** Mother Base and the Afghanistan and Africa free roam. Story missions, the hospital prologue, side ops and cutscenes are **not** co-op yet; see the [campaign roadmap](docs/CAMPAIGN-ROADMAP.md).

## Download

Get `DiamondDogsTogether-<version>.zip` from [Releases](https://github.com/TrollTaylor/diamond-dogs-together/releases). Both players need the **same version**.

## Requirements

- MGSV: The Phantom Pain on Steam, version 1.0.15.4, with **no other mods**.
- Windows 10 or 11, and Steam running and logged in.
- A save that can **Continue** into Mother Base or free roam (not one still in the hospital opening).

## Install

1. In Steam, right-click MGSV and choose **Manage → Browse local files**. This opens the game folder, the one with `mgsvtpp.exe`.
2. Extract **everything** from the zip into that folder: `DiamondDogsTogether.exe`, `dinput8.dll` and the `mgs-coop` folder.
3. Run **DiamondDogsTogether.exe**.

The first start sets everything up:
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
- **Vehicles:** your partner drives; you can ride in the back seat of a jeep. In Afghanistan and Africa free roam, a world vehicle that nobody drives stays where it was left on both screens and has the body damage the host's PC gives it; either player's Fulton of it removes it on both screens, and one destroyed on the host's PC disappears from the partner's screen.
- **Helping each other:** revive a downed partner, wake a sleeping one, and carry them.
- **Shared world:** enemy kills, sleeps and Fultons, alerts, time and weather.
- **Shared progress:** the host's travel, checkpoints, and game over with continue for both players.
- **Online play:** through Steam, with no port forwarding.

## Known limits

- Story missions, the hospital prologue, side ops and cutscenes are not co-op yet.
- **Enemies:** each PC still moves its own enemies. Only what happens to them (killed, asleep, Fultoned, alerted) is shared.
- **Vehicles:** trucks that enemies drive move separately on each PC. Wheel damage and fire are not shared. Damage the guest does to a vehicle is undone within about 10 seconds, and a vehicle the guest destroys stays destroyed only on the guest's screen. A vehicle destroyed on the host's PC leaves no wreck on the guest's screen. In Mother Base a parked vehicle's position, damage, Fulton and destruction are not shared yet.
- **Animals and prisoners** are not shared.
- **Sortie vehicles:** a vehicle you bring from sortie prep (the helicopter's vehicle choice) doesn't appear on your partner's screen; they see you without it. Use world vehicles you find on the map to ride together.
- **Mounts:** you can't shoot from a back seat, and a partner riding D-Horse or D-Walker is hidden.
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

`tools/package.ps1 -Friend` builds the release zip from a passing `tools/worker/Invoke-WorkerChecks.ps1 -Profile Release` run. Developer docs:
- [Status and change log](docs/STATUS.md)
- [Campaign roadmap](docs/CAMPAIGN-ROADMAP.md)
- [Partner body](docs/native-puppet.md)
- [World sync](docs/world-sync-protocol.md)
- [Two-PC play](docs/two-pc-play.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

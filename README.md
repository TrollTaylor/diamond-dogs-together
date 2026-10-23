<h1 align="center">
  <img src="docs/readme/logo.png" alt="Diamond Dogs Together" width="300">
</h1>

<p align="center">
  Two-player co-op for <b>Metal Gear Solid V: The Phantom Pain</b><br>
  <b>0.14.0</b> &nbsp;·&nbsp; two players &nbsp;·&nbsp; Windows &nbsp;·&nbsp; Steam
</p>

<p align="center">
  You and a friend each run your own copy of the game. You see each other move, take cover, drive, and hide in Mother Base, Afghanistan, and Africa free roam.
</p>

<p align="center"><i>A development build. The hospital, story missions, side ops, and the endings are still ahead.</i></p>

<br>

<p align="center">
  <img src="docs/readme/afghanistan-together.jpg" alt="Venom Snake watches his partner walk ahead through the grass of Afghanistan free roam. The partner's name floats above them." width="100%">
  <br>
  <i>Afghanistan free roam. The name above the other soldier is your partner.</i>
</p>

## In the field

<p align="center">Frames from the 0.13.5 sessions on 22 October 2026.</p>

<table>
  <tr>
    <td width="50%" valign="top">
      <img src="docs/readme/jeep-driver.jpg" alt="A partner drives a jeep through an Afghan village. D-Dog runs beside the vehicle.">
      <br><br>
      <b>They drive</b><br>
      A teammate in a jeep or truck sits in the driver's seat on your screen, and the vehicle moves smoothly.
    </td>
    <td width="50%" valign="top">
      <img src="docs/readme/back-seat.jpg" alt="Two players share one jeep in a village: one at the wheel, one in the back seat.">
      <br><br>
      <b>The back seat</b><br>
      Press Action beside their jeep to ride along. Your camera stays outside the car. Press Action again to get out.
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <img src="docs/readme/cardboard-box.jpg" alt="A partner hides inside a cardboard box in a grassy field while the other player lies prone in front.">
      <br><br>
      <b>The box</b><br>
      When they hide in a cardboard box, you see the box move with them, and it comes off when they leave it.
    </td>
    <td width="50%" valign="top">
      <img src="docs/readme/mother-base-ladder.jpg" alt="On a Mother Base deck, one player stands on the concrete while their partner climbs a ladder on a red container.">
      <br><br>
      <b>Ladders</b><br>
      On Mother Base their body climbs the ladders, including the ones on the containers.
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <img src="docs/readme/ledge-climb.jpg" alt="A partner climbs a rocky ledge in Afghanistan while the other player watches from the grass below.">
      <br><br>
      <b>Ledges</b><br>
      They pull up onto waist-high rocks and ledges with the game's own climb.
    </td>
    <td width="50%" valign="top">
      <img src="docs/readme/crouch-and-ddog.jpg" alt="Two soldiers move through Afghanistan grass, one crouched, with D-Dog nearby.">
      <br><br>
      <b>On foot</b><br>
      Walk, run, crouch, go prone, aim, and reload. D-Dog can be out with you.
    </td>
  </tr>
</table>

## Play with a friend

<table>
  <tr>
    <td width="33%" valign="top">
      <p align="center"><b>1 · Install</b></p>
      <p align="center">Both of you copy the zip's contents into the game folder and start <b>DiamondDogsTogether.exe</b> once. Your real save is backed up and left alone.</p>
    </td>
    <td width="33%" valign="top">
      <p align="center"><b>2 · Host</b></p>
      <p align="center"><b>Play</b> → <b>Host</b> → <b>Host game</b>. Send the code, or invite a Steam friend from the game.</p>
    </td>
    <td width="33%" valign="top">
      <p align="center"><b>3 · Join</b></p>
      <p align="center"><b>Play</b> → <b>Join</b>, paste the code, <b>Join game</b>. Both Continue into the same place.</p>
    </td>
  </tr>
</table>

Both of you need the same things:

- **MGSV: The Phantom Pain** from Steam, version **1.0.15.4**, with no other mods. Diamond Dogs Together stops if the game files are not the original ones.
- This **same 0.14.0 version** on both PCs. An older version will not connect.
- Steam running, each of you logged into your own account.
- A save that can **Continue** into Mother Base or free roam. A save still in the hospital opening always Continues into a story mission, and co-op cannot join that.
- A way to talk (Discord, Steam chat) so the host can send a join code. Steam friends can use an invite instead.
- Windows 10 or 11.

The game stays offline. **"Failed to log in"** on the title screen is expected. Konami's servers are never contacted.

### Install once (drag and drop)

1. Quit MGSV.
2. Open the game folder: in Steam, right-click the game and choose **Manage → Browse local files**. It is the folder with `mgsvtpp.exe`.
3. Copy everything from the zip into that folder: `DiamondDogsTogether.exe`, `dinput8.dll` and the `mgs-coop` folder. Let Windows replace files if it asks.
4. Start **DiamondDogsTogether.exe** from the game folder.
5. The first start runs a short setup. It checks the game files, backs up your real saves (they are only read), makes a **copy** of your save for co-op, and builds `mgs-coop\mgs-coop-player-runtime.dat` from your own game files. Nothing is downloaded, and that file is never inside the zip.

After that, starting MGSV from Steam is still your normal game, on your real saves. Co-op turns on only for a game started from **Diamond Dogs Together**.

### Every time you play

Start **DiamondDogsTogether.exe** and open **Play**.

**Host.** Choose **Host** and press **Host game**. The host always plays as Snake. A join code is copied to the clipboard, something like `MGS2-…@your Steam id`. Send it to your friend, and keep it private while the game is running. A new code is made every time you host. Or, once you are in the game, press **F9 → Partner / Co-op → Online → Invite a friend**.

**Friend.** Choose **Join**, pick who you play as (**Story: Quiet or Ocelot**, **Ocelot**, a **Diamond Dogs soldier**, or **your custom character**), paste the code and press **Join game**. Or tick **Wait for a Steam invite instead** and accept the host's invite in the overlay (**Shift+Tab**) while this co-op game is running. Accepting an invite while the mod is not running starts ordinary MGSV on your real saves. Quit that and join from Diamond Dogs Together first.

**Both of you.** Click through the opening screens, choose **Continue**, and meet in Mother Base or in Afghanistan or Africa free roam. Stay near each other. A partner who is far away is hidden until you meet again.

Leave Diamond Dogs Together open: its **In game** page shows the connection, the ping, where your partner is, and the save sharing. The same news appears in the game as a **CO-OP** line at the top right. If you close the window, a hidden helper still switches co-op off after the game exits.

Quit MGSV normally when you are done. Co-op switches itself off, and the next launch from Steam is your real save again.

### Same point in the story

Co-op pairs when both games are in the same Mother Base, free roam, or helicopter.

If the joiner's co-op save would start somewhere else, the host's save is copied across at the title screen and MGSV restarts once on it. Keep Diamond Dogs Together open. That takes about a minute. The host's save is shared only once it already continues into Mother Base or free roam. Your own co-op copy is kept aside. **Tools → Share save → Restore previous** brings it back. Your real saves are never part of this.

To share by hand instead: the player who already has Mother Base quits the game and uses **Tools → Share save → Export**, which puts a zip on the Desktop. The other player quits, uses **Import...**, and picks that zip.

## What works in 0.14.0

In Mother Base (`30050`) and Afghanistan and Africa free roam (`30010`, `30020`):

- Your partner's body, face, camouflage, and weapons. They walk, run, crouch, go prone, aim, reload, and switch guns. Aim works with toggle aim too.
- Their name above them.
- A grenade or an empty magazine actually leaves their hand. It is the animation only: no second grenade appears, and your own grenade count does not change.
- The cardboard box, ladders, ledge climbs, and the standing pickup.
- Driving. You see them in the driver's seat of a jeep or truck, and the vehicle updates smoothly. While they drive, you can no longer climb into that same driver's seat (that used to make a second driver and hide them).
- The back seat of a light vehicle, with your own camera behind the car. Trucks and armored vehicles have no passenger seat: while they drive one of those, you stay out.
- Wounds and sleep. A gun injury leaves you crawling, unable to aim or fire, until a teammate revives you. A friendly sleep dart puts you to sleep with your health kept, in the game's own sleep. Sleep ends when a teammate wakes you, or after 30 seconds.
- Wake or revive from within **3 meters**: close F9 if it is open, and hold **Action** (**E**, or **Y** after you have used a controller) for **3 seconds**. A short interruption drains the bar instead of starting it over. A revived player has 3 seconds of protection.
- Carry. Hold **Carry** (**R**) beside a wounded or sleeping teammate, walk, and hold it again to put them down. The carried player's camera follows from behind the carrier.
- D-Dog. The guest's dog follows the host's dog. Attacks, spotting, and the dog's health stay on each PC.
- The world, when you leave the default session alone. Alerts, the clock, the weather, the host's travel, checkpoints, and recovering together from **MISSION FAILED**. The joiner follows the host's travel. Doing the same trip again within a short window goes alone, and **F9 → Partner / Co-op → Travel lock** turns the lock off for that session.
- Online play through Steam, with no port forwarding. Steam connects you directly or through its relays. A code works between any two players. An invite works between Steam friends.
- One program, **DiamondDogsTogether.exe**: setup, Host / Join, the connection status, Share save, Collect logs and Uninstall. The host is Snake; the joiner picks Story (Quiet while she is on your team, Ocelot otherwise), Ocelot, a Diamond Dogs soldier, or their custom character.

Each of you shoots your own copy of the world. You see the other player's muzzle flash, sound, aim, and reload. The hits that count are the ones fired on the PC where that player is sitting.

## Still unfinished

- Story missions, the hospital opening, side ops, rewards, and the endings.
- A third or fourth player.
- Aiming, shooting, or a first-person view from the back seat.
- D-Horse and D-Walker. While your partner rides either one, their body is hidden and comes back when they get off. You do not see a horse or a walker under them, and you cannot share one mount.
- Getting out of the cardboard box. The box comes off, without the exit motion.
- Hand signs, knocking, wall cracks, and pipes.
- C4. In online play your partner does not yet kneel to place a charge or press the detonator. That animation exists only in the developer test mode.
- A slow walk (**Ctrl+W**) can play the run animation in place. That is an older issue, still here.
- A fresh two-PC session on this exact build. Two copies on one machine have been through these scenes, including a simulated bad connection. The last session a player reported from two separate houses was **0.12.0**.

## If it does not connect

The **In game** page of Diamond Dogs Together and the in-game **CO-OP** line usually say why.

- `link=connecting` for a long time: the joiner cannot reach the host. Both PCs need to be online in Steam, the host's game needs to be started from Diamond Dogs Together, and **Accept joins** needs to be on.
- `Your partner runs Diamond Dogs Together X; you run Y`: use the same version on both PCs.
- Different missions: meet in the same Mother Base, free roam, or helicopter. If one game always Continues into a story mission, share a save first.
- A red banner, **Co-op did NOT start and save protection is NOT active**, means this launch is ordinary play on your real saves. Quit if that is not what you wanted.

The zip's `mgs-coop\README.txt` has the short version; [two-PC play](docs/two-pc-play.md) has the details of the connection.

To send a report: quit the game, use **Tools → Collect logs**, and send the zip it puts on the Desktop. Join codes, the link key, and your Windows user name are removed. Steam ids are cut down to the last four digits.

## Your saves

Co-op plays on a copy made at install. Your real save files are backed up and are not written during co-op games. Progress does not cross between the two: co-op progress stays in the copy, and the copy does not pick up later progress from a normal game.

When you want the mod gone: quit MGSV, start Diamond Dogs Together, and use **Tools → Uninstall**. It switches co-op off, removes `dinput8.dll` (only when it is this version's), sets the co-op settings aside, and checks that your real saves still match the backup from the first setup. It does not overwrite saves. Then delete `DiamondDogsTogether.exe` and, once you no longer need them, the `mgs-coop` folder with the backup, the co-op copy, and the logs.

## Building it

The Diamond Dogs Together zip is the way to play. Building from this checkout is for working on the mod.

Clone with submodules, then prepare the pinned source dependencies. Lua 5.1.5 is used only by the test fixtures. The script checks the Lua archive and the MinHook revision, and it does not launch or install the game.

```powershell
git clone --recurse-submodules https://github.com/TrollTaylor/diamond-dogs-together.git
cd diamond-dogs-together
pwsh -File tools/Prepare-Dependencies.ps1
./tools/build.ps1 -Configuration Release
```

You need CMake, the Visual Studio C++ build tools with an x64 Windows SDK, and PowerShell. `-BuildOnly` compiles without the test run. A source build does not by itself prove a gameplay change. Exact game-script checks need your own licensed Lua extract in `MGS_GAME_LUA_DIR`.

`tools/package.ps1 -Friend` writes `dist/DiamondDogsTogether-<version>-<stamp>/DiamondDogsTogether-<version>.zip` (and the same files unpacked in `package/`). The zip's root is the game folder: `DiamondDogsTogether.exe`, `dinput8.dll`, and `mgs-coop/` with the licences, `README.txt`, `package.json`, and `source.zip`. Both programs come from one passing `tools/worker/Invoke-WorkerChecks.ps1 -Profile Release` receipt. The runtime archive is built on each player's PC and is never put in the zip. Developer only: `Play-Offline-Coop.bat` runs the two-window test on one PC (`tools/dev`), and `DiamondDogsTogether.exe --write-session host|join` / `--end-session` write and remove the session files without Steam for hidden-worker checks.

Adapted third-party code, including Dynamite, is listed with versions and licenses in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Where to go next:

- [Current status and evidence](docs/STATUS.md) — what 0.14.0 changed, what was checked, and what is still open.
- [Two-PC play](docs/two-pc-play.md) and [Steam online](docs/steam-online.md) — the connection, in detail.
- [Debug menu](docs/debug-menu.md) — F9, once you are in a co-op session.
- [Campaign design](docs/campaign-design.md) — the full game, as planned.
- [Native partner](docs/native-puppet.md) — how the other player's body is driven.

<details>
<summary>Older notes</summary>

The `docs/` folder is also the lab notebook: world sync, animation rounds, the save handoff, and the Steam and engine investigations. Those notes describe how a piece was checked. They are not instructions for playing.

</details>

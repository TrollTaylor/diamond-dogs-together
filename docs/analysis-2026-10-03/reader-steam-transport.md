## 1. What the game uses for the co-op/FOB transport (verified by static analysis of the 1.0.15.4 exe)

**Method.** I parsed the PE with pefile and disassembled with capstone, using scratch scripts in my temp dir. I found every direct `FF15/FF25` call and `mov reg,[rip]` reference to each steam_api64 IAT slot in `.xtext`, then tracked the returned interface pointer to its vtable call. Slot names come from the exact-version headers in gbe_fork's `sdk/steam/` (isteamnetworking005.h, isteamuser018.h, isteamfriends015.h, isteamutils007.h, isteamremotestorage012.h). No interface pointer from a getter is ever stored to a global. `.xdata`, `.ecode` and `.text` contain no references to the Steam IAT. Wrapping the 13 getter imports therefore intercepts all of the game's Steam use, within the limits of this scan.

**Transport.** The transport is the legacy ISteamNetworking P2P API used by the engine class SteamUdpSocketImpl (Dynamite's `FoxNioImplSteamUdpSocketImpl*`, relocated):
- Send `0x1A5FB60` → `SendP2PPacket(remote=[addr+8], data, len, 0 /*Unreliable*/, 0 /*channel*/)`, returns len or -1.
- Recv `0x1A5FAC0` → `ReadP2PPacket(buf, cap, &len, &sender, 0)`; the sender is written into the address object at +8.
- Poll `0x1A5FBF0` → loops `IsP2PPacketAvailable(&n, 0)` and posts a synchronous readable event each iteration.
- Accept `0x1A5F740`, Close `0x1A5F780`, and GetP2PSessionState `0x1A5F8C0`. The last one converts the remote IP/port into a fox::nio address via 0x1A516E0/0x1A51750.
- Constructor `0x1A5F470` registers 1202 and 1203. The handlers `0x1A5FA10` and `0x1A5F960` do **not** accept directly; they queue Fox events. The packet manager's `0x1CEE420` then finds a free record among 16 slots, calls Accept, and sets the record to state 2.

**Live corroboration.** The only live networking receipts in steam-observer.log come from caller 0x1A5FC15 (the poll), and they begin 16 ms after host creation (log line 329, tick 557886984; the host was created at tick 557886968). Each host run shows them on **two threads**. No send, read or accept has ever run live because no peer has ever connected.

**Not used:** lobbies (SteamMatchmaking is not imported), BeginAuthSession/EndAuthSession (peers never validate each other), AllowP2PPacketRelay, CloseP2PChannelWithUser, and NetworkingSockets/Messages. GetAuthSessionTicket[13] at 0x1DFEAB6 serves only the master-server login. Other registered callbacks are 1101-1103, 333, 152, 163 and 331.

## 2. Local identity and persona

- `0x13A0` (vtable slot 0x20A0940 method) runs, in order: CreateMutexW, SteamAPI_Init (on false, exit(1)), GetSteamID through a hidden return pointer, then the validity check `0x1B10` (on false, exit(1)). The fake ID therefore must be a valid individual/public account ID, for example `0x01100001xxxxxxxx` with instance 1, which also passes fuse's range check.
- `0x4CE0A0` is the local account cache: SteamID at +0, persona name from SteamFriends::GetPersonaName at +8 (strcpy_s 0x81), BLoggedOn at +0xB0. It is reached through vtable 0x2169330 slot 2 (thunk 0x494190). **Its timing relative to the mod's install point is unknown.** That ordering decides whether a post-DirectInput wrapper can fake B's identity.
- Remote names come from RequestUserInformation[37] followed by GetFriendPersonaName[7] (0x7D7A3D, 0x7D7D97). The overlay call ActivateGameOverlayToUser("steamid", id) is at 0x1DFEE04.

## 3. Second-instance blocker

At 0x13D6-0x141A: `CreateMutexW(&sa_nullDACL, FALSE, L"METAL GEAR SOLID V: THE PHANTOM PAIN")`. If GetLastError()==ERROR_ALREADY_EXISTS (0xB7), the code at 0x1595 calls ReleaseMutex and CloseHandle and returns false, aborting startup. There is no FindWindow or OpenMutex import anywhere (checked both import sets, including the protector's lowercase-DLL IAT at 0x9E7D000). The handle global is at 0x2B58FC0.

## 4. Loopback test-mode design (not implemented)

**Overview.** Both instances run the shim. Instance A (host) keeps its real SteamID. Instance B (guest, from MGS_TPP_guest) reports a fake SteamID. All P2P traffic between configured peer IDs goes over 127.0.0.1 UDP; no real Steam networking calls are made in loopback mode.

**Getter wrappers**, built on the existing `find_import_slot` and `exchange_import`:

| IAT slot | Role | Overrides |
|---|---|---|
| 0x208E138 SteamNetworking | both | all 22 slots emulated (0, 1, 2, 3, 4, 6 used by the game); slots 8-21 return failure |
| 0x208E128 SteamUser | guest | [2] GetSteamID (hidden-ptr ABI); [6] GetUserDataFolder; [13] GetAuthSessionTicket (rewrite bytes 12..19 for fuse); everything else forwarded |
| 0x208E110 SteamFriends | guest (A also benefits) | [0] GetPersonaName; [7] GetFriendPersonaName; [37] RequestUserInformation returns false; [29] overlay swallowed for loopback IDs |
| 0x208E118 SteamRemoteStorage | guest | [0], [1], [3], [10], [12] redirected to a per-identity folder |
| 0x208E0F8 / 0x208E0F0 Register/UnregisterCallback | both | track CCallback* with id 1202 or 1203; always forward |
| 0x208E0E8 RunCallbacks (already wrapped) | both | after the original returns, drain the synthetic queue and call `cb->vtbl[0](cb, &payload)` after vptr, flag and id checks |
| 0x208D170 CreateMutexW | guest, DllMain-time | rename the exact title mutex, then restore the slot |

**Steam semantics to emulate.** Send implicitly accepts the session. A first inbound packet from an unknown peer queues the packet and posts 1202 once (packets are dropped after 10 s if not accepted). Unaccepted packets must never appear in IsP2PPacketAvailable, otherwise the loop at 0x1A5FC50-0x1A5FCA5 can hang. Read truncates to the caller's buffer. Unreliable sends are capped at 1200 bytes. GetP2PSessionState returns active with IP 0x7F000001 and the peer's port. Close tears down the session and notifies the peer. Remote loss triggers 1203 with error 4.

**Transport details.** The shim state takes a lock, because the poll runs on two threads. The receive thread does no game calls. Disable SIO_UDP_CONNRESET and use 1 MB buffers. Each datagram carries a header of magic, version, type, mode, channel, src, dst and length. Datagrams are only accepted from configured peer endpoints. Optional latency and loss simulation plus a bounded trace log are useful.

**Connecting the pieces.**
- B's client-host.txt holds A's real ID; the native client path writes it at connect-info +0xB8, so B's SendP2PPacket(A) is mapped to 127.0.0.1:portA.
- On A, the first datagram from fakeB produces 1202. The packet manager accepts and creates the record, and later ReadP2PPacket reports the sender as fakeB.
- Both instances should use fuse: patch disp32 at 0x7D346F, the same site tpp-mod patches.
- B's saves and cloud are redirected, and B's achievements and leaderboards optionally suppressed.

**Timing check.** Log the getter wrappers' return RVAs. If 0x4CE0D3 has already run before install, move the guest SteamUser/SteamFriends slot writes into the minimal DllMain step. Then verify the exe fingerprint later at the normal install point and roll back on mismatch. Arxan behavior with pre-entry patches is unverified.

## 5. Comparison with Goldberg / gbe_fork

**gbe advantages:** mature, complete emulation, including P2P callbacks and GetP2PSessionState with IP (dll/steam_networking.cpp). It supports two instances on one PC through its port+i bind loop. Identity and name come from configs.user.ini, saves and cloud are local, and no vtable code is needed. The source is already cloned at build/gbe_fork (327815e), and steam_interfaces.txt is already generated, though in the wrong folder.

**gbe drawbacks:**
- It must replace steam_api64.dll in **both** instances, so both need copies; the main install must be preserved per AGENTS.md. A gbe instance cannot talk to a real-Steam instance.
- Real Steam behavior is not exercised at all.
- Konami login fails with gbe tickets, so fuse plus the URL redirect is mandatory.
- The project's Steam DLL hash and getter-byte gates (steam_observer.cpp:198-227) disable the networking observer, so an exemption is needed.
- Saves default to a shared GSE path, so each copy needs its own local_save_path.
- The interaction with the old interface strings in the protected code (SteamClient015/SteamUser017/STEAMAPPS006) is unknown.
- It is not built yet.

**Recommendation:** make the in-mod shim primary, because it is small (the game touches only six networking slots plus a few identity slots), keeps the real DLL and real login for A, and exercises the production path. Keep gbe as the fallback. Either way, finish with a real two-account test.

## 6. Verification levels

- Verified by static analysis (this pass): import tables, all vtable-slot uses, the mutex logic, the GetSteamID ABI and validity check, the callback IDs and CCallback layout/vtables, the P2PSessionState offsets, the URL patch site, and no Steam-IAT references from protected sections.
- Verified live (existing logs): observer installs, and poll-getter receipts on two threads during host sessions.
- Compiled or fixture-tested only: steam_packets/steam_runtime adapters.
- Speculation or unverified:
  - timing of 0x4CE0A0 relative to the install point;
  - Arxan's response to DllMain IAT patches;
  - SteamAPI_Init succeeding in a second same-account process;
  - Konami server behavior with concurrent same-account logins;
  - the purpose of the old interface strings in .xdata;
  - whether the original 2015 SteamFriends015 vtable lacks slots 70-71.
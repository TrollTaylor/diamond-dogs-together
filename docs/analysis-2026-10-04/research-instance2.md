Instance 1 offline: realization, driving and spike plan (static analysis only; nothing was run)

Scope: static analysis of mgsvtpp.exe 1.0.15.4. Addresses are RVAs. Repo, Dynamite source and game files were only read; nothing was changed or launched. Helper scripts (pefile/capstone loader plus a verified Fox StrCode64 implementation) are in the scratchpad at C:/Users/taylo/AppData/Local/Temp/claude/D--Games-steamapps-common-MGS-TPP/e7a6e70e-f530-4f59-b4a3-60e868204a66/scratchpad/p2/exe.py and foxhash.py.

1. What exists for instance 1 offline
- **Pointer chain.**
  - P = *(base+0x2A82C30) is the secondary view of the player game object; enclosing = P-0x20. Offset rule: enclosing+X = P+(X-0x20).
  - Q = *(P+0x60) is the per-instance player-data component (hash 0xc39b191343f7, class vtable 0x23AD970).
  - S = *(Q+0x1E8) is a data block holding per-instance bitsets and positions.
- **What allocation creates** (0x9BD7E0, offline): totalCount 2 (P+0x208), realizedCount 2 (P+0x20C), 2 instance blocks (P+0x210, at *(P+0x378), stride 0x6A8), and local index 0 (P+0x214, always 0 offline). It also creates a realized bitset (*(P+0x380)), a realizer (*(P+0xA0)), and Q/S arrays for 2 instances. The synchronizer (*(P+0xA8)) is NOT created; that needs online bit 8.
- **Instance 1 is not realized.** Expected state: ids u16 *(P+0x70)[1] = 0xFFFF and pool *(P+0x78)[1] = 0xFF. UpdatePartsStatus skips it (this is the guard at 0x9CD160), so no body and no model. No offline code sets its realize inputs.

2. What realizes a remote instance (FOB) and how it is driven
- **Per-frame chain.** The player update (0x9C1F00) runs the realizer (0xBF7140), which asks a policy object (0x121B5E0). The bitset is committed, ids and pools are rebuilt from it, and the block's realize method (0x9C85D0) builds the body. UpdatePartsStatus (0x9CD030) then loads parts from Q's appearance arrays.
- **Policy conditions for a non-local instance i.** All must hold:
  - its bit in the S[0] exists bitset is set;
  - its position *(S+0x20)[i] lies inside the loaded area;
  - either bits Q+0x194 and Q+0x198 for i are both set, or all-instance-active mode is on (P+0x204 bit 0x2000) and 0x9B3FE0(i) returns true;
  - realizedCount is 2 or more.
- **Who supplies these online.**
  - 0x12CC5F0 → 0x12CE0F0 sets the S[0] bits from session membership.
  - The applies 0x12CC790 and 0x12A5810 set Q+0x194/0x198 when appearance data arrives, and fill Q's appearance arrays plus the dirty bit Q+0x180.
  - The transform receive 0x12A53A0 writes S+0x20[i] from a packed 64-bit position: 1 cm units around an origin (likely SetWorldCenter, SPEC) and an 11-bit yaw.
- **Inputs to the remote body.** S+0x20, Q+0x1F0 position history (SetPosition 0xFE0080), Q+0x10 interpolated position (0xFE02F0), the motion records Q+0x1D8/0x1E0/0x1F8, and the arrays Q+0x18 (action), Q+0x30 (stance), Q+0x38 (move level). The record updater 0x12CE870 turns these into PlayerInfo records for every instance (the bank at image+0x2BD3FF0). That bank is an output: useful for checking, useless for driving.
- **Mode flags.** 0x2000 = `Player.SetAllInstanceActiveMode` (0x9DCD20). 0x4000 = `Player.SetOtherInstanceIsForMirrorMode` (0x9DF320). With both set, the local appearance is mirrored onto the other instances.
- **Per-instance setter.** Warp at 0x9CEAE0 (P vtable +0x1B0), reached through the TppPlayer2 SendCommand handler 0x9C5F80. It writes S+0x20[idx] and moves the body if realized. It has no bounds check, and `pos` must be a plain 3-number table.

3. Commit 554cc4f (realizePlayer2)
- It sent `{id="Realize"}` then Warp to `{type="TppPlayer2", index=1}`.
- "Realize" is not a TppPlayer2 command; the only handled ids are Warp, WarpAndWaitBlock, WarpToStation and two unnamed ones.
- On the host, Warp(1) only wrote S+0x20[1] of an instance that was never realized. On the guest, index 1 is the guest's own player, so it teleported the guest and its camera.
- It ran only on session roster events. Commit 2fd15ab removed it for these reasons.

4. Ranked ways to make it visible and move
- **A (recommended spike)**, on the Lua thread at 30050, with R = 1 - P+0x214 (i.e. 1):
  1. `Player.CopyPartsInfoFromLocalPlayer{instanceIndex=R}`, or `SetPartsInfoAtInstanceIndex` with the partner's appearance.
  2. `Player.SetAllInstanceActiveMode(true)`, so the game sets the exists bits every frame.
  3. A native receipt does `Q+0x194 |= 1<<R` and `Q+0x198 |= 1<<R`.
  4. Each tick: `GameObject.SendCommand({type="TppPlayer2",index=R},{id="Warp",pos={x,y,z},rotY=rad})`, with the pose served by native as the receipt's returned chunk.
  - Then check ids/pools, GetPlayerType and PlayerInfo record R.
  - Guards: validate P, Q and S non-null, P+0x204 bit0 set, count == 2, and R < count; re-derive pointers every tick.
  - Risks: possible null-synchronizer use in non-local body code (untested), trap crashes on a non-local player (Dynamite precedent; the repo has a trap patch), and teleport-only motion.
- **A2:** the same, all native, calling 0x9CEAE0 directly.
- **B:** smooth animation by writing the synchronizer's target slots directly (S+0x20, Q.vt+0x18 and +0x20, the stance/move/action arrays, the motion records). The setters are confirmed; which ones the remote body reads needs a live probe first.
- **C:** mirror mode, only as an appearance sanity test.
- **D:** feeding the synchronizer applies. Not recommended: the component only exists in online mode.
- **E:** NPC puppet fallback. Not analysed here.
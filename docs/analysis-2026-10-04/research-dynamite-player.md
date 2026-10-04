UPSTREAM DYNAMITE: HOW PLAYER 2 APPEARS AND MOVES

Labels:
- STATIC: source read, or read-only disassembly of the installed 1.0.15.4 exe
- UPSTREAM-WEB: author comments from the public GitHub API
- SPEC: inference

Source: work/upstream-dynamite, a shallow clone at c90f252 that targets 1.0.15.3. Its addresses do not carry over to 1.0.15.4.

1. Core finding
Dynamite never positions, animates or realizes the partner in its own code. The game's FOB multiplayer systems do all of that once a real FOB session exists. Dynamite only makes a story mission look like a two-player FOB:
- a session
- S_IS_ONLINE set before Mission.Start
- two TppPlayer2 instances via fox2
- the FOB motion and equipment-ghost assets
- one mission-ID patch (DoesFobGhostPlayer)
- TppCoder.SetWorldCenter
Dynamite's own C++ only reads partner positions (PlayerInfoService) and warps the local player (WarpToPartner).

2. What is local and what needs a session
Local (no session needed):
- two instances: two_player2_instances.fox2 (partsBlockCount=2, two_players_game_obj.fpk) with single_player2_instance.fox2 removed
- player2_online.mtar and player2_location_motion.fpk
- CreateEquipGhostBlockGroups({ghostCount=1}) plus a larger equipment mission block
- the S_IS_ONLINE status flag, which makes the player setup at 0x9BAA3D set P+0x204 |= 0x100; upstream reports the partner is invisible without this bit
- TppCoder world center
- index-addressed TppPlayer2 commands (WarpAndWaitBlock)

Session-dependent (out of scope under the new direction):
- transport (fox::nt over Steam P2P)
- index assignment (host 0, client 1)
- the FOB ghost-parts path: DoesFobGhostPlayer 0x9C1710 and 0x9C14D0 both require a session query == 2 in addition to count==2, bit 0x100 and mission 50050
- the player synchronizer's per-index packet input
Consequence: our installed ghost patch (0x9C176A) has no effect offline.

3. Invisible-player history
- #13/#17 (UPSTREAM-WEB): network position codec clamp. Positions travel as (pos - worldCenter)*100 integers, about ±1310 m on x/z. Story maps had the world center at (0,0,0), so the partner was clamped, fell through the ground and looked invisible. Fixed in commit 5780e04 with SetWorldCenter in 18 sequence OnEnter functions.
- TppDefine: mission 13 needed a larger equipment block before the host could render the client.
- #59: still open, partner invisible.
- #116: walker gear and rider invisible, glides with no animation.
- The commented-out upstream patch shows that removing the S_IS_ONLINE setup check makes players invisible.

4. TppCoder in 1.0.15.4 (STATIC)
- SetWorldCenter 0x545DA0 and GetWorldCenter 0x545D50 use the global at 0x2BD7BA0; native getter 0x545D40; registered into service struct +0x68 (0x993058).
- Zeroed at mission initialization (0xA1E96F).
- Set natively only by Mother Base cluster code (0xBEA806).
- The player synchronizer (constructor 0x12A3B20, factory 0x12A3DF0, size 0x168) caches the world-center pointer at +0x38.
- Encoders 0x12A3FC0/0x12A7AE0 use the ×100 constant at 0x20AA7B8.
- Decoder 0x12A53A0: 18/17/18-bit position plus 11-bit rotation, ×0.01 (constant 0x20F7640), then adds the world center.
Implication: if our netcode sends float transforms, the world center does not matter. It only matters if we reuse the game codec.

5. Indices
Session-assigned: host = instance 0, client = instance 1, hardcoded across Dynamite. Offline (SPEC), both games likely report local index 0, so use remote = 1 - P+0x214.

6. DynamiteSyncImpl
- Format: "DYNAMITE" magic + FlatBuffers {packet_num, union}, at most about 1 KB, stale packets dropped, no retransmit, single peer.
- Transport: the game's own Steam UDP socket, borrowed from the last game send.
- Handlers run on the receive thread.
- No pose messages.
The framing pattern carries pose data easily over our own UDP. Run handlers through a game-thread queue instead of on the network thread.

7. Recommended next steps
Spikes, in order:
- A: Lua WarpAndWaitBlock on instance 1, plus SetPartsInfoAtInstanceIndex, plus realization logging.
- C: NPC puppet fallback driven by SetWarpPosition/SpecialAction.
- B: native Warp and motion mapping for proper animation.
Messaging: build on the DynamiteSync-style framing over udp_transport.

Hardcoded 50050 checks in 1.0.15.4 player code: 0x9B66BC, 0x9BBC4E, 0x9C175E, 0x9CCF04, 0xAECCDB (synchronizer: 0x12A8E2A, 0x12A9B90).
Partial list of native S_IS_ONLINE readers: 0x9BAA35, 0x9E5524, 0x9E7A48, 0x1340845, and others listed in the findings.

Scratch analysis helper (read-only): C:/Users/taylo/AppData/Local/Temp/claude/D--Games-steamapps-common-MGS-TPP/e7a6e70e-f530-4f59-b4a3-60e868204a66/scratchpad/dynup/xr.py
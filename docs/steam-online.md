# Steam online co-op

Design, verification record and implementation of the Steam online transport
(the user's request: online play "like Seamless Co-op", no port forwarding
and no external program). Checked by static analysis only: no game
was launched and no Steam call was made while writing it.
`tools/generate_steam_shim_contract.py --check` and `--steamclient` repeat the
binary checks; section 19 records how the implementation follows this design
and where it deliberately differs.

**Status (2026-10-08): implemented, fixture-tested, never run in the game.**
WP-A (ABI, transport, acquisition and gates, owner pass, self-test, contract
modes, `steam_transport_tests`), WP-B (S1 code join: lab.ini keys and bits,
the SteamAPI_Init wrapper with 1251, the accept policy, partner_net transport
selection and status, Play-Coop online host/join, Collect-Logs, docs) and WP-C
(S2: the F9 Online panel, `InviteUserToGame`, 337 with a waiting guest and
`ArmSteamGuest`, the rich-presence beacon) are in. WP-D (S3, command-line
join) needs a user decision: only its pure extractor exists, unwired. The
coordinator's hidden self-test (section 14) and the two-PC acceptance
(section 15) are pending. Section 20 (2026-10-09) adds the function-pointer
dispatch for code joins between non-friends, the guest's "why it failed"
(1252), plain-language reasons, "partner left", the in-game status line and a
commit build id.

## 0. Decision

Link v2 runs **unchanged** on top of **ISteamNetworkingMessages `"SteamNetworkingMessages002"`**:
handshake, HMAC, sessions, reliable channels, DATAGRAM world sync and mission follow all stay as they are.
The interface comes from `ISteamClient017::GetISteamGenericInterface` in the running `steamclient64.dll`.

Steam does the signaling and the NAT traversal, and falls back to Valve's relay network (SDR). Players
need no port forwarding, no VPN and no extra program. Valve's relays are the "official servers". Konami
servers are never used.

How players find each other, in shipping order:
- **S1:** an `MGS2` join code (host SteamID64 + secret), pasted into Play-Coop.
- **S2:** a Steam invite sent from the F9 menu. The guest accepts it in the Steam overlay or chat, which
  posts callback 337. A code can also be pasted into F9.
- **S3 (optional; needs a user decision):** accepting an invite starts MGSV directly into co-op.

Steam lobbies are never used. The UDP link (MGS1 codes) stays as the explicit LAN/VPN choice. Nothing
switches transports automatically. The one-PC lab stays on UDP; there, Steam is exercised only by the
self-test in §14.

| Requirement | Sections |
|---|---|
| (1) transport | §6, §7, §8 |
| (2) UX | §10 |
| (3) roles, world sync and mission follow unchanged | §8.4 |
| (4) real accounts | §9.1 |
| (5) safety | §2, §5 |
| (6) tests | §13, §14 |
| (7) package and docs | §16 |

## 1. Verification record

### 1.1 Binaries inspected

| File | Size | SHA-256 | Notes |
|---|---|---|---|
| `MGS_TPP/mgsvtpp.exe` | 166,517,760 | `085c2f82…bb45` | equals `steam_observer` `accepted_sha256` |
| `MGS_TPP/steam_api64.dll` | 211,368 | `08bd0152…c60a` | TimeDateStamp 0x555B7ACF (2015-05-19), SizeOfImage 0x38000, 747 exports, SDK about 1.33. Interface strings: `SteamClient017`, `SteamUser018`, `SteamFriends015`, `SteamNetworking005`, `SteamUtils007`, `SteamMatchMaking009` |
| `C:/Program Files (x86)/Steam/steamclient64.dll` | 26,328,216 | `caba4826…7fee` | TimeDateStamp 2026-09-03 01:34Z, ImageBase 0x138000000, SizeOfImage 0x197D000. Valve updates it, so it is **not pinnable** (§5, §12) |

No Valve SDK headers exist in the repo. Layouts were therefore taken from the SDK and then confirmed
against steamclient's own code: `memset` sizes, field stores, name strings and RTTI.

### 1.2 steam_api64.dll (hash-pinned): verified

| Export | RVA | Bytes / decoded meaning |
|---|---|---|
| `SteamAPI_Init` | 0x5420 | `33 C9 E9 49 F5 FF FF` (jmp 0x4970) |
| `SteamAPI_RunCallbacks` | 0x58A0 | caches `GetISteamUtils` (client +0x48) → `RunFrame` (utils +0x70) → `jmp 0x2BA0` → 0x3320 |
| 0x3320 (internal run) | – | non-atomic reentrancy byte at 0x30C45; the mutex guards only the call-result list. Callback loop: `Steam_BGetCallback` → dispatch → `Steam_FreeLastCallback` |
| dispatch | 0x2DE0..0x2DF9 | `movzx eax,[cb+8]; shr al,1; and al,1; cmp bpl,al; … call [vt+8]`. Filters on the GameServer bit, calls `Run(p)`, **never calls `GetCallbackSizeBytes`**. Call results (0x3494) do check the size; we use none |
| `SteamAPI_RegisterCallback` | 0x5530 → 0x2B10 | `or byte [cb+8],1; mov [cb+0Ch],id`, then a multimap insert with **no lock** |
| `SteamClient` | 0x5D90 | `48 8B 05 A1 AF 02 00 C3` (global 0x30D38, the same one RunCallbacks uses) |
| `SteamAPI_GetHSteamUser` | 0x5270 | `8B 05 DA BB 02 00 C3` (0x30E50) |
| `SteamAPI_GetHSteamPipe` | 0x5260 | `8B 05 DE BB 02 00 C3` (0x30E44, RunCallbacks' pipe) |
| `SteamAPI_ISteamClient_GetISteamGenericInterface` | 0x1450 | `48 8B 01 48 FF 60 60`: slot 12, tail jump |
| `SteamAPI_ISteamUser_BLoggedOn` | 0x1950 | `48 8B 01 48 FF 60 08`: slot 1 |
| `SteamAPI_ISteamUser_GetSteamID` | 0x2030 | `lea rdx,[rsp+30h]; call [rax+10h]; mov rax,[rax]`: slot 2, hidden return, returns uint64 |
| `SteamAPI_ISteamFriends_GetFriendCount` | 0x10A0 | `jmp [rax+18h]`: slot 3 |
| `…_GetFriendByIndex` | 0x1300 | hidden return, `call [rax+20h]`: slot 4 |
| `…_GetFriendRelationship` | 0x1C80 | `call [rax+28h]`: slot 5 |
| `…_GetFriendPersonaName` | 0x16C0 | `call [rax+38h]`: slot 7 |
| `…_GetFriendGamePlayed` | 0x1680 | `call [rax+40h]`: slot 8 |
| `…_SetRichPresence` | 0x1D90 | `jmp [rax+158h]`: slot 43 |
| `…_ClearRichPresence` | 0x1E20 | slot 44. **Never called** |
| `…_GetFriendRichPresence` | 0x13E0 | `call [rax+168h]`: slot 45 |
| `…_RequestFriendRichPresence` | 0x15D0 | `call [rax+180h]`: slot 48 |
| `…_InviteUserToGame` | 0x14B0 | `call [rax+188h]`: slot 49 |

### 1.3 steamclient64.dll (current build): verified

**How the interface is produced.** The interface table entry `"SteamNetworkingMessages002"` at .data
0x16FCDE8 points to factory 0x715F30. The factory gets the engine object (`[engine+0x258](hUser,hPipe)`).
On null it returns null; otherwise it **allocates a new 16-byte adapter** `{vptr 0x12EAAC8, inner}`.
**Every `GetISteamGenericInterface` call allocates a new adapter, so we acquire once and cache.**

**Adapter vtables, found through RTTI:**

| Adapter | vtable | Slots | Notes |
|---|---|---|---|
| `.?AVCAdapterSteamNetworkingMessages002@@` | 0x12EAAC8 | **exactly 6** | each slot is a pure forwarder `mov rcx,[rcx+8]; mov rax|r10,[rcx]; jmp [rax+8N]` to inner slot N |
| `.?AVCAdapterSteamNetworkingSockets012@@` | 0x12EB7A8 | 47 | |
| `.?AVCAdapterSteamNetworkingUtils004@@` | 0x12EBE00 | 25 | |
| `.?AVCAdapterSteamClient017@@` | 0x12E4990 | 37 | slot 12 → 0x70F200 → internal `(hUser, hPipe, version)` |

**Messages002 implementation.** The implementation is `CSteamNetworkingMessages@SteamNetworkingSocketsLib`
(its `IClientNetworkingMessages` subobject at +0xD0, vtable 0x1360E68). Each method starts with the GNS
global lock, acquired as `call 0xAB3530` tagged with the method name, so the API is thread-safe.

| Slot | Method | Verified detail |
|---|---|---|
| 0 | `SendMessageToUser` (0xA7E1D0) | `rdx` = identity (type 0 → returns 8 InvalidParam); `r9d` = cb, at most 0x80000; flags are the 5th argument (`test byte [rsp+0x120],20h`, the AutoRestartBrokenSession path, logged); channel is the 6th argument |
| 1 | `ReceiveMessagesOnChannel` (0xA7D610) | `edx` = channel, `r8` = out array, `r9d` = max |
| 2 | `AcceptSessionWithUser` (0xA7AEF0) | |
| 3 | `CloseSessionWithUser` (0xA7BC90) | |
| 4 | `CloseChannelWithUser` (0xA7B980) | `r8d` = channel |
| 5 | `GetSessionConnectionInfo` (0xA7CA10) | `memset(info, 0, 0x2B8)`; quick status zeroed for 0x78 bytes; returns `[conn+0x220]`; the info copy starts at `conn+0x170`, so **the state is at info+0xB0** |
| 6 | internal (memory validation) | not exposed by the adapter |

**Message object** (`AllocateMessage` 0xAA2CD0):

| Offset | Field |
|---|---|
| +0x00 | `pData` |
| +0x08 | `cbSize` |
| +0x10 | identity |
| +0xB0 | `pfnFreeData` |
| +0xB8 | `pfnRelease` |
| +0xC0 | channel, initialized to −1 |
| +0xD0 | lane |

The object is 0x108 bytes. This matches the SDK public prefix.

**Sockets012 slots** (inner functions found by name string):

| Slot | Method | Detail |
|---|---|---|
| 5 | `CloseConnection` | the adapter normalizes the bool at `[rsp+28h]` |
| 11 | `SendMessageToConnection` | |
| 14 | `ReceiveMessagesOnConnection` | |
| 19 | `CreateSocketPair` (0xA8D3B0) | `rdx` = out1, `r8` = out2, `r9b` = loopback, `[rsp+28h]` and `[rsp+30h]` = identities. A null identity becomes an **IP-localhost identity: type 1, cbSize 0x12** |

**Utils004 slots:**

| Slot | Method | Detail |
|---|---|---|
| 0 | `AllocateMessage` | |
| 1 | `GetRelayNetworkStatus` | the inner copies 0x110 bytes |
| 2 | `GetLocalPingLocation` | named |
| 7 | `CheckPingDataUpToDate(float)` | float in `xmm1`. The SDK's inline `InitRelayNetworkAccess()` is `CheckPingDataUpToDate(1e10f)` |

**Strings.** `k_nVirtualPort_Messages` is a dedicated virtual port above 0xFFFF
(`m_nRemoteVirtualPort <= 0xffff || == k_nVirtualPort_Messages`). Messages sessions are therefore
separate from any ordinary or legacy port. `Callback_MessagesSessionRequest` is present (a config value
name; not used).

### 1.4 mgsvtpp.exe: verified

**Steam imports (13).** These are the IAT slots:

| IAT slot | Import |
|---|---|
| 0x208E0D8 | `UnregisterCallResult` |
| 0x208E0E0 | `RegisterCallResult` |
| 0x208E0E8 | `RunCallbacks` |
| 0x208E0F0 | `UnregisterCallback` |
| 0x208E0F8 | `RegisterCallback` |
| 0x208E100 | `UserStats` |
| 0x208E108 | `Utils` |
| 0x208E110 | `Friends` |
| 0x208E118 | `RemoteStorage` |
| 0x208E120 | `Apps` |
| 0x208E128 | `User` |
| 0x208E130 | `Init` |
| 0x208E138 | `Networking` |

The exe imports no `SteamClient`, `SteamMatchmaking` or `Shutdown`.

**Call sites:**
- `SteamAPI_Init`: one call site, 0x1428 (WinMain start; already pinned as `exe_identity_block`).
- `RunCallbacks`: one call site, 0x7DED5D (return address 0x7DED63).

**RegisterCallback sites (9), with the ID passed in `edx`:**

| Site | ID |
|---|---|
| 0x7D8D9A | 1101 |
| 0x7D8DD7 | 1102 |
| 0x7D8E14 | 1103 |
| 0x80C3F3 | 333 |
| 0x80D705 | 152 |
| 0x1A5F4EE | 1202 |
| 0x1A5F52F | 1203 |
| 0x1DFE9CA | 163 |
| 0x1DFEC4C | 331 |

**The game never registers 337, 1221/1222, 1251, 1252 or 1281.** Callbacks with those IDs that Steam
posts into the game's pipe find no handler.

**The game's ISteamFriends use.** The game calls only slots 0, +0x18, +0x20, +0x38, +0x128 and +0xE8. It
never sets rich presence, never invites, and never opens the invite dialog.

**Strings.** The exe contains no `SteamNetworkingMessages`, `…Sockets` or `…Utils` strings.
`+connect_lobby` is at file offset 0x222DD50.

The research's claims about the 333 handler internals (0x80C480 …) were not re-verified. They are not
load-bearing, because we never touch 333.

### 1.5 Corrections to the research

| # | Research claim | Verdict | Correction |
|---|---|---|---|
| C1 | After an invite "the host knows the guest's ID and sends first" | Wrong for link v2: the guest initiates and the host has nothing to send | The host **polls `AcceptSessionWithUser(id)` at 4 Hz** for known IDs (invited friends, the current or last guest, beacon hits). This needs no callback and sends no traffic; the call returns false when nothing is pending |
| C2 | Invite string `+mgscoop_join <hostid64> <S>` | Second format, second parser | One format: `+mgscoop_join MGS2-…-CC@<id64>`, with one strict parser for F9 paste, 337 and the command line. The code's host must equal 337's `m_steamIDFriend` |
| C3 | "vptr and every used slot … in an executable section" | Imprecise | The vptr must lie in a **read-only, non-executable** section (.rdata) of steamclient64; the slots must lie in `.text`. Add an **RTTI name check** (`vptr[-1]` → COL → TypeDescriptor name) and, for Messages002, the **forwarder-shape check** |
| C4 | (none) | Missing | Every `GetISteamGenericInterface` call leaks a 16-byte adapter, so acquire **once per interface per process** |
| C5 | Self-test runs "the real SteamTransport code with only the legs swapped" | A null-identity socket pair carries IP-localhost identities (type 1), which a SteamID gate rejects | Split SteamTransport into a **core** (bounds, Release-once, delivery, idle cadence) and **legs** (Messages leg: SteamID identity gate; Connection leg: handle → synthetic endpoint, asserts type 1, cbSize 18) |
| C6 | Self-test: "nothing leaves the PC"; network-loopback pair always run | Not guaranteed | Messages calls may lazily start Steam's own networking (relay discovery, certificate, Valve infrastructure only, never a peer). A network-loopback pair binds UDP sockets and could trigger a **Windows Firewall prompt on the visible desktop**, so it becomes opt-in (`steam_selftest=2`) and is never part of default hidden runs |
| C7 | Endpoint `{account, 0x5354}`, logs `steam:…1234` | `FormatPeer` / `MaskAddress` would print a bogus IPv4 | Make status and log formatting transport-aware (§11) |
| C8 | "Session logic unchanged (wrong_endpoint …)" | With a constant port the guest's port check passes for anyone | The guest's Messages leg delivers **only** the configured host SteamID; that guest session runs with `exact_filter=true` |
| C9 | Sends from the game thread under one SRW lock straight into steamclient | That puts the game thread under the GNS global lock | Use a **send ring**: the game thread copies the datagram, and the transport thread drains the ring. Game threads never enter steamclient for networking |
| C10 | `Sleep(1)` when idle | Waits 15.6 ms unless timeBeginPeriod is set | Use a high-resolution waitable timer at 2 ms (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`), falling back to `Sleep(1)` |
| C11 | (none) | Missing | **Exit hygiene:** after the existing window-close shutdown flag, the transport thread makes one CLOSE plus `CloseSessionWithUser`, then makes no further Steam calls. This avoids a terminated thread holding the GNS lock while steamclient detaches |
| C12 | Rich presence "only mgscoop* keys" | Incomplete | Remove a key with `SetRichPresence(key, "")`. **Never call `ClearRichPresence`** |
| C13 | Pin the 333 handler bytes | Redundant with the exe SHA-256 | Keep the callback-ID set and string-absence assertions. Add the export pins of §1.2, add `--check`, and add the `--steamclient` structural mode (§12) |
| C14 | Guest not running: "stay pass-through" | UX trap: accepting starts **plain MGSV on the real saves** | FRIEND-GUIDE ordering ("start Play-Coop → Join online first"); S3 is the real fix |
| C15 | Route from slot 5 | Info layout unverified | Info is 0x2B8 bytes (verified). State at +0xB0 equals the return value (verified). `m_nFlags` at +0x1B8 (`Relayed` = 16) comes from the SDK and is used only after the state and identity cross-check; otherwise `route=unknown`. Quick status is always null |
| C16 | Host accepts any requester at most 4/s | Accepted strangers can park traffic on unread channels | Cap accepted non-peer identities at 8. Close them after 15 s without a decodable link header and deny them for 60 s |
| C17 | Lab stays UDP | OK | Lab mode **refuses** `net_transport=steam`; Steam in the lab is the self-test only |
| C18 | Friends enumeration in the RunCallbacks hook | Hundreds of IPC calls would hitch a game thread | The owner hook spends **at most 4 Steam calls per pass**, enumerates only while the F9 Invite page is open, and refreshes at most every 5 s |
| C19 | Utils `GetRelayNetworkStatus` = slot 1 | **Correct** | I first expected `InitRelayNetworkAccess` there; it is an inline, so the research is right |

All other ABI claims in the research were verified as stated:
- the flat wrappers, slots 1, 2, 12, 43–49 and 3–8;
- Messages002 slots 0–5 and the 0x21 flag semantics;
- callback base 1250 → 1251/1252;
- the message offsets;
- the identity layout (136 bytes, pack 1, type 16, cbSize 8);
- Sockets012 slots 5, 11, 14 and 19;
- the unlocked RegisterCallback, and the dispatcher's missing size check.

## 2. Invariants (safety)

- **Never touched:**
  - `ISteamNetworking005`: getter 0x208E138, its slots and channels, callbacks 1202/1203, and the
    0x1A5F4xx–0x1A5FCxx paths;
  - callback 333, lobbies, `+connect_lobby`, `ActivateGameOverlayInviteDialog`, `InviteUserToLobby`;
  - `SteamAPI_Init`, `Shutdown` and `RunCallbacks` are never *called* by us; the game's callbacks are
    never unregistered or replaced (the GNS `ISteamNetworkingSockets012::RunCallbacks` of section 20
    is a different function: it drains only the queue our own function pointers fill);
  - `GetAuthSessionTicket` / `BeginAuthSession`;
  - `ClearRichPresence`, and any rich-presence key that is not `mgscoop_*`;
  - S_IS_ONLINE, the FOB synchronizer, game sessions, the Konami master/login/version checks, and fuse;
  - real saves; on-disk `steam_api64.dll`, `steamclient64.dll` and `steam_appid.txt`.
- Steam online runs only in `mode=play` with `saves=redirected`, exactly like UDP play.
- **Steam calls are confined to three places:**
  1. the pinned steam_api64 flat exports (User, Friends, Client) from the **owner context**, the
     post-RunCallbacks hook (§10.3);
  2. Messages, Sockets and Utils interface calls from **our transport and self-test threads only**;
  3. `SteamAPI_RegisterCallback` from the Init wrapper (§6.1) only.
- Every new path refuses with `link=disabled; reason=steam_<x>` and leaves the game untouched. Nothing
  falls back silently to another transport.
- The secret, codes, keys and the 337 connect string are never logged. SteamIDs are logged masked
  (`steam:…1234`).

## 3. Architecture

```
Game threads ─ RunCallbacks (0x7DED5D) ─► observed_callbacks ─► [real RunCallbacks]
                                               └─► steam_online::OwnerPass()   (try-lock, ≤4 Steam calls)
                                                   - one-time interface acquisition, gates, persona/friends,
                                                     InviteUserToGame, rich presence, 337 parse handoff
  steam_api dispatcher ─► our CB objects (1251 / 337) ─► bounded queues (enqueue only)

Lua thread ─ partner_net::Start ─► transport = UdpTransport | SteamTransport
  game thread ─ SubmitLocal/SendReliable ─► link::Session (g_tx_lock) ─► LinkTransmit ─► send ring

SteamTransport thread (one per instance):
  drain send ring → SendMessageToUser(…, 0x21, kChannel)
  ReceiveMessagesOnChannel(kChannel, 32) ×≤4 → leg filter → sink.on_datagram → Release
  drain the accept queue → AcceptSessionWithUser; poll-accept known IDs at 4 Hz
  sink.on_idle every 20 ms (LinkIdle: timers, retransmits, status, world hooks)
  route poll at 1 Hz; idle non-peer cleanup; shutdown flag → CLOSE, CloseSession, park
```

## 4. ABI header `native/include/mgscoop/steam_net_abi.hpp` (new, pure)

The header follows `steam_abi.hpp` conventions: offsets, IDs and slot indices with static_asserts, and
no reproduced Valve class.

**Interface versions:**
- `"SteamNetworkingMessages002"`, `"SteamNetworkingSockets012"`, `"SteamNetworkingUtils004"`;
- the expected RTTI names `.?AVCAdapterSteamNetworkingMessages002@@`,
  `…Sockets012@@`, `…Utils004@@` and `.?AVCAdapterSteamClient017@@`.

**Identity** (`#pragma pack(1)`, 136 bytes):
- +0 type (16 = SteamID, 1 = IP);
- +4 cbSize (8 for a SteamID);
- +8 SteamID64;
- the rest is zero.

The builder zero-fills the whole structure. The checker requires type 16, cbSize 8 and
`is_individual_steamid(id)` (`(id>>32)==0x01100001 && (id&0xffffffff)!=0`).

**Message offsets:**

| Field | Offset |
|---|---|
| data | 0x00 |
| size | 0x08 |
| identity | 0x10 |
| pfnRelease | 0xB8 |
| channel | 0xC0 |

`Release(m)` is `(*(void(**)(void*))(m+0xB8))(m)`, called exactly once.

**Connection info:** size 0x2B8, identity at 0, state at 0xB0, idPOPRelay at 0xAC, flags at 0x1B8,
Relayed = 16. Connection states: 0 None, 1 Connecting, 2 FindingRoute, 3 Connected, 4 ClosedByPeer,
5 ProblemDetectedLocally.

**EResult values:** 1 OK, 3 NoConnection, 8 InvalidParam, 25 LimitExceeded.

**Send flags:**
- `kMsgFlags = 0x21` (UnreliableNoNagle | AutoRestartBrokenSession) for Messages;
- `kConnFlags = 1` for the self-test connections (AutoRestart is Messages-only).

**Constants:** `kLinkChannel = 0x4D47` (never 0); `kSteamPortTag = 0x5354`; max message 1200 (link v2's
datagram cap; GNS fragmentation is fine).

**Callbacks:**

| ID | Callback | Payload |
|---|---|---|
| 1251 | SessionRequest | identity, 136 bytes |
| 1252 | SessionFailed | not registered |
| 337 | `GameRichPresenceJoinRequested_t` | `{u64 friend @0; char connect[256] @8}`, 264 bytes |

**Interface ABI classes.** These are abstract classes with exact declaration order, no overloads and a
protected non-virtual destructor. MSVC therefore emits `call [vptr+8N]`. Fakes in the tests implement
the same classes.

| Interface | Methods (slot) |
|---|---|
| `ISteamNetworkingMessages002Abi` | the 6 methods of §1.3 (0–5) |
| `ISteamNetworkingSockets012Abi` | only CloseConnection (5), SendMessageToConnection (11), ReceiveMessagesOnConnection (14), CreateSocketPair (19). Unused slots are declared as placeholders so the indices are exact; a static check verifies the count |
| `ISteamNetworkingUtils004Abi` | slot 1 `GetRelayNetworkStatus(void*)`, slot 7 `CheckPingDataUpToDate(float)`, with placeholders between |

**Callback object (hand-built, no C++ virtuals).** The struct is
`{const void* const* vptr; uint8_t flags; uint8_t pad[3]; int32_t id;}`, with static_asserts for
+8/+0xC. Its static vtable has three entries:
- slot 0 `Run(self, p, bIOFailure, hCall)`: no-op;
- slot 1 `Run(self, p)`;
- slot 2 `GetCallbackSizeBytes` (136 / 264).

Flags start at 0, so the GameServer bit stays clear.

## 5. Acquisition and gates (owner context, once)

Gates run in this order. The first failure latches `steam=disabled; reason=<r>`, then
`link=disabled; reason=steam_<r>`, and no transport starts.

1. **`api`:** the observer's full SHA-256 of `steam_api64.dll` and its module path, plus every export of
   §1.2 used here at its pinned RVA with its pinned bytes (from `steam_shim_contract.hpp`).
2. **`not_initialized`:** `SteamClient()`, `GetHSteamUser()` and `GetHSteamPipe()` are non-zero.
3. **`client_module`:** `GetModuleHandleW(L"steamclient64.dll")` is set, and its path lies under the
   registry `SteamPath` (HKCU\Software\Valve\Steam). The section table is read from the mapped image.
4. **`client_iface`:** `SteamClient()`'s vptr is in a read-only non-executable section of that module;
   slot 12 is in `.text`; the RTTI name is `.?AVCAdapterSteamClient017@@`.
5. **`offline`:** `BLoggedOn()` is true. **`self_mismatch`:** `GetSteamID()` equals `net_steam_self`.
   **`peer_is_self`:** the guest's host ID differs from self.
6. **`iface_missing`:** `GetISteamGenericInterface(hUser, hPipe, "SteamNetworkingMessages002")` is
   non-null. The result is cached forever (C4).
7. **`iface_module`** and **`iface_rtti`:** the Messages adapter vptr is in .rdata with the expected RTTI
   name. **`iface_shape`:** slots 0–5 are in `.text`, and each one's first bytes decode to the forwarder
   `48 8B 49 08` + (`48 8B 01 48 FF 60 xx` | `4C 8B 11 49 FF 22` (N=0) | `4C 8B 11 49 FF 62 xx`) with
   `xx == 8N`. The inner object (`[adapter+8]`) vptr must also be in .rdata, and inner slots 0–5 in
   `.text`. All reads are bounded to the module image.
8. **Play only:** `Utils004` acquired with the same module and RTTI checks, then
   `CheckPingDataUpToDate(1e10f)` once to warm SDR (shortens the first connect). If Utils fails, the
   warm-up is skipped and nothing else is refused.
9. **`fault`:** every interface call goes through a tiny C-style `__try/__except` thunk (no C++ objects in
   the frame). An exception latches `steam=disabled; reason=fault`, stops the transport, and makes the
   status say "restart the game". The log notes that steamclient may be left unusable for this process.

A Steam update that changes adapter generation fails gate 7. That is deliberate. Play-Coop then says
"Steam networking changed; use a LAN/VPN code ([L]) and send Collect-Logs".

## 6. Callbacks and session acceptance

### 6.1 Registration: an Init IAT wrapper installed in DllMain

`InstallEarlyLabPatches` adds the IAT slot 0x208E130 wrapper **only** when lab.ini has `mode=play`,
`net=1` and `net_transport=steam`, and the save redirect is in place (without it play is refused, so no
callback is registered either). The pins it checks are:
- the exe `exe_identity_block` (already pinned);
- `SteamAPI_Init` at 0x5420 (`33 C9 E9 49 F5 FF FF`);
- `RegisterCallback` at 0x5530 and its 0x2B10 body;
- the dispatch bytes.

On any mismatch, no wrapper is installed and `early.log` gets `steam_callbacks=refused; reason=<r>`.

**The wrapper:**
- runs on the main thread at 0x1428, before the game creates any thread that runs callbacks;
- calls the original Init and returns its result unchanged;
- only when the result is true, calls `SteamAPI_RegisterCallback(&g_cb1251, 1251)` on the host, or
  `(&g_cb337, 337)` on the guest;
- uses static objects that are never unregistered;
- logs `steam_callbacks=registered; ids=1251|337` (written by the early logger after return).

**Interaction with other code:**
- `VerifyEarlyLabPatches` covers the slot.
- `steam_observer` sees `early_patches::InitWrapperInstalled()`, logs
  `symbol=SteamAPI_Init status=early_wrapper` and skips its own exchange.
- Every other mode is byte-identical to today.
- If the wrapper is refused, the host runs `accept=poll_only` (§6.4) and the guest cannot receive 337 (its
  F9 paste still works).

### 6.2 Handlers (game callback threads; enqueue only)

The handlers never block, log or call Steam. Their `noexcept` bodies run under `__try`.

- **1251:** read 136 bytes. Require type 16, cbSize 8 and an individual SteamID. Push the account onto a
  16-entry MPSC queue (SRW lock held only for the push), coalescing duplicates. The queue drops when full
  (counted).
- **337:** copy 264 bytes into a single-slot mailbox (newest wins, at most 1 per 2 s). Parsing happens on
  the transport thread (§9.4). The payload is copied to the stack **before** the lock is taken, so a
  fault while reading Steam's buffer (caught by the handler's `__except`) can never leave the lock held
  (that would block the 1251 handler on the game's callback thread and the transport thread forever).

### 6.3 Host accept policy (transport thread)

1. **Queue (`via=callback`):** accept while `accepting` is true. At most 4 new identities per second,
   and at most 8 accepted identities that are not the session peer. Skip denied identities.
2. **Poll (`via=poll`) at 4 Hz:** `AcceptSessionWithUser(id)` for the current or last guest, invited
   friends (≤ 16, kept 10 min) and beacon hits (§10.4). A false result is normal. These are ids the
   host chose, so a poll **uses no rate token** (the 4/s limit is for unsolicited 1251 requests; a 4 Hz
   poll of an invited friend who is not joining would otherwise use up every token and starve a code
   join). `AcceptSessionWithUser` also returns **true for an established session** (steamclient
   0xA7AEF0 returns 1 whenever the session has a connection and only accepts a connecting one), so
   only a session not already marked live is counted and logged as `steam=accept`.
3. **Hygiene:** a non-peer identity whose messages never decode as a link v2 header for 15 s gets
   `CloseSessionWithUser` and a 60 s deny entry (64 entries). Former peers get closed after 60 s idle.
4. HMAC (K_hs) still decides who actually joins. The session's busy, rejected and rate logic is unchanged.

### 6.4 Guest

The guest sends HELLO first, so it needs no accept. Its leg delivers only messages whose identity is the
configured host (C8). Other identities are dropped (`steam_foreign`), and after 3 of them in 10 s the
guest calls `CloseSessionWithUser`.

## 7. `lab::SteamTransport` (`native/steam_transport.{hpp,cpp}`)

### 7.1 Surface

The class is constant-initializable, trivially destructible, and can be instantiated more than once (the
self-test runs two). Its methods are the subset `partner_net` uses:
- `start(const SteamTransportConfig&, const DatagramSink&)`
- `transmit_to(Endpoint, chunks, count, droppable)`
- `stop()`, `running()`, `error()`
- `local_port()`, which always returns 0
- `stats()`, a `TransportStats` superset

The config carries the role, an injected `SteamNetApi` (interface pointers plus a fault-latch callback),
the leg, the host ID (guest) and `idle_interval_ms`.

**Legs:**
- **Messages leg (play):** send with `SendMessageToUser(identity(Endpoint), data, n, 0x21, kLinkChannel)`;
  receive with `ReceiveMessagesOnChannel(kLinkChannel, msgs, 32)`. The identity gate maps the account to
  `Endpoint{account, 0x5354}`.
- **Connection leg (self-test only):** send with `SendMessageToConnection(h, data, n, 1, nullptr)`;
  receive with `ReceiveMessagesOnConnection(h, msgs, 32)`. The handle maps to a fixed synthetic endpoint,
  and the identity must be type 1 with cbSize 0x12.

### 7.2 Send path (any thread under `g_tx_lock`)

The send ring is 64 slots × 1200 bytes (static, about 77 KB). It is single-producer: `g_tx_lock`
serializes callers, the receive thread's `LinkIdle` included.

- `transmit_to` copies into a free slot and returns true. When the ring is full, it counts `ring_full` and
  returns true: the datagram is lost, and link v2 recovers.
- Datagrams of size 0 or above 1200 return false.
- The transport thread drains the ring before each receive pass.
- Send results are counted per EResult. OK counts as `tx`; 3 and 25 count as `send_fail` (logged at most
  once per 10 s).

### 7.3 Receive loop

Each pass:
1. Drain the send ring.
2. Call `Receive…(32)` up to 4 times.
3. For each message:
   - bounds-check: 1 ≤ size ≤ 1200, data non-null, identity valid for the leg;
   - `sink.on_datagram(ctx, data, size, from)`;
   - `Release(m)`.

   `Release` runs exactly once, even when the message is rejected. There is no copy.
4. Accept work (§6.3).
5. `on_idle` when 20 ms have elapsed (deadline-driven, as in UDP).
6. Route poll (1 Hz).
7. If nothing was received: high-resolution timer wait of 2 ms (C10).

The loop never allocates.

### 7.4 Endpoint mapping

`Endpoint{address = account, port = 0x5354}`, so `SteamID64 = 0x0110000100000000 | account`. The
per-address HELLO rate table, `wrong_endpoint`, `rebound` and `TrustedPeer` keep working unchanged.

### 7.5 Route and status

For the current peer only, at 1 Hz:
- call `GetSessionConnectionInfo(id, &info /*0x2B8 zeroed*/, nullptr)`;
- the result is valid only if the return value equals `info+0xB0` and the identity at `info+0` equals the
  peer;
- if valid, `route = (flags@0x1B8 & 16) ? relay : direct`, plus `steam_state = <name>`;
- otherwise `route=unknown`.

### 7.6 Shutdown and fault

- **Shutdown flag** (set by the existing window-close hook; the window thread only flips the flag): the
  thread sends one link CLOSE if connected, waits 30 ms (no Steam call; `CloseSessionWithUser` closes
  without linger, so the queued CLOSE gets a chance to leave first), calls `CloseSessionWithUser(peer)`,
  then parks in `WaitForSingleObject(INFINITE)`. There are no further Steam calls.
- **Fault latch** (§5.9): the thread parks immediately.

## 8. `partner_net` integration

### 8.1 Transport selection

`partner_net::Config` gains `transport` (udp|steam), `steam_peer` (account), `steam_wait` and
`accepting`. A small ops table `{transmit_to, stats, local_port}` replaces the direct `g_transport` uses
in:
- `LinkTransmit`
- `FormatStatus`
- `OnIdle`
- `StartLink`

The legacy raw path stays UDP-only (play already refuses legacy). Steam session config:
- host `peer` unused;
- guest `peer = {host_account, 0x5354}`;
- `exact_filter = true` (C8);
- `play = true`.

### 8.2 Waiting guest and `ArmSteamGuest`

With `net_steam_wait=1`, Start runs the transport but no session, does not read `link-secret.txt`, and
reports `link=waiting`.

`ArmSteamGuest(host_id, secret)` runs on the transport thread, from a 337 hand-off or an F9 paste mailbox.
It is allowed only in state `waiting` or `rejected`:
1. derive K_psk, zero the buffers;
2. under `g_tx_lock`: `Session::Start(guest config)` plus the same receive and reliable reset as a new
   session;
3. clear the world queues through `on_session`.

A connected or connecting guest ignores 337 (logged `steam=join_ignored; reason=busy`).

### 8.3 Host secret retention

The host keeps the 20-character secret in one static buffer while hosting. It is used only to build the
code for F9 "Copy code" and invites, and is zeroed on the shutdown flag. It is never logged.

### 8.4 Above the transport

The following are unchanged:
- `SubmitLocal`, `SubmitLook`, `SubmitInput`;
- `SendReliable` / `DrainReliable` (channels 1–3), `SendDatagram`, `WorldHooks`;
- roles, world sync and mission follow.

`LinkIdle` and the world hooks now run on the Steam transport thread at the same 20 ms cadence.

## 9. Configuration, codes and parsers

### 9.1 lab.ini keys (play only unless noted)

| Key | Values | Default | Rule |
|---|---|---|---|
| `net_transport` | `udp` \| `steam` | `udp` | lab + `steam` → forced `udp`, +1 invalid, `net_transport=udp; reason=steam_play_only` (the lab never uses Steam networking) |
| `net_steam_self` | SteamID64 | – | written by Play-Coop; must be individual; checked at gate 5 |
| `net_peer_steam` | SteamID64 | – | guest; individual and not self |
| `net_steam_wait` | `0` \| `1` | `0` | guest without a peer: waiting mode (S2) |
| `net_steam_accept` | `on` \| `off` | `on` | host: initial `accepting` (F9 toggles it) |
| `steam_selftest` | `0` \| `1` \| `2` | `0` | **lab only**; 2 adds the network-loopback pair |

**New play refusals** (forced, bit set, +1 invalid):

| Bit | Condition | Effect |
|---|---|---|
| 0x40000 | `steam_selftest` in play | |
| 0x80000 | `net_peer_steam` on a host | ignored |
| 0x100000 | steam guest without `net_peer_steam` and without `net_steam_wait=1` | `net=0`, `link=disabled; reason=no_peer_steam` |
| 0x200000 | UDP endpoint keys (`net_peer_addr`, `net_listen_port`, `net_bind`) with `steam` | ignored |

With steam, the play-default UDP keys are not applied. There are no fake-identity keys (they are already
refused), so both PCs use their real accounts (requirement 4).

### 9.2 MGS2 join code

Format: `MGS2-SSSSS-SSSSS-SSSSS-SSSSS-CC@<SteamID64>`.
- S: 20 canonical Crockford characters, the existing secret.
- CC: the first 10 bits of `SHA256("MGS2|" + S + "|" + <id64 decimal>)`, with the same `c1`/`c2` rule as
  MGS1.
- The id64 is 17 decimal digits with no leading zero and must be individual.
- Parse tolerance is as for MGS1: case, blanks, dashes, O→0, I/L→1.

Statuses: `ok, empty, bad_prefix, bad_char, bad_length, bad_check, bad_steamid`.

The parser lives in three places with shared vectors:
- PowerShell: `ConvertFrom-CoopSteamJoinCode`, `New-CoopSteamJoinCode`;
- **native** (new invariant: the DLL now parses MGS2, for F9 and 337): pure
  `native/include/mgscoop/steam_join_code.hpp` + `.cpp`, built on `link::Sha256`;
- the vectors in `tests/friend_package_tests.ps1` and the native tests.

A short all-base32 variant (account in 7 characters) was considered and rejected. Codes are pasted
anyway, and the visible SteamID lets the guest see whom they join.

### 9.3 Invite string and 337

- **Invite:** `+mgscoop_join ` followed by the MGS2 code (at most 70 bytes; the Steam limit is 256).
- **337 parser:** the payload must be NUL-terminated within 256 bytes and printable ASCII. It must have
  the exact prefix `+mgscoop_join `, then a valid MGS2 code with nothing after it. The code's host ID must
  equal the payload's `friend` field.

Outcomes:
- success: `steam=join; via=invite; host=steam:…1234`;
- failure: `steam=join_refused; reason=<status>`;
- buffers are zeroed afterwards.

### 9.4 S3: command-line join (optional; needs a user decision)

Install-Coop asks once for consent and writes `mgs-coop\steam-join.ini` (`steam_join=1`); Uninstall
removes it.

DllMain then accepts a play-guest configuration from the command line, but only when all of these hold:
- the consent file exists;
- `GetCommandLineW` contains exactly one `+mgscoop_join <MGS2>`;
- no lab.ini exists.

The configuration is built in memory: `mode=play; role=guest; save_dir=<coop-saves>; transport=steam;
peer and secret from the code; world=3; the shipped partner defaults; offline master_url`. No file is
written. All existing gates still apply (saves redirected, or plain MGSV).

Risks:
- the secret sits on the process command line, visible to the same user's processes;
- the game's parsing of an unknown argument is unproven (two-PC test).

Until S3 ships, the guest must start Play-Coop → Join online **before** accepting an invite (C14).

## 10. UX

### 10.1 Play-Coop

Menu:
- `[O] Host online (Steam, recommended)`
- `[J] Join online`
- `[L] LAN/VPN (UDP code)`: the current MGS1 flow, byte-identical
- `[Q] Quit`

**Host online:**
- Preflight: `Get-SteamActiveUser` ≠ 0 (else "Start Steam and log in"); id64 = 76561197960265728 +
  ActiveUser.
- Fresh secret, MGS2 code shown and copied to the clipboard (cleared at exit as today).
- lab.ini `net_transport=steam`, `net_steam_self`; no UDP keys, no UPnP, no public IP, no firewall rule.
- Text: "Send this code to your friend, or invite them in game: F9 → Partner / Co-op → Online → Invite."

**Join online:**
- Read-Host: paste a code (MGS2, or MGS1 to fall through to the UDP join), or press Enter to "wait for an
  invite".
- A code writes `net_peer_steam` + `link-secret.txt`. Waiting writes `net_steam_wait=1` and no secret.
- A code whose host ID equals our own is refused ("that is your own code").

**Launch evidence:**
- Required: the existing lines plus `link=config; mode=play; role=…; proto=2; bind=steam; port=0; …`.
- Failure: a `link=disabled; reason=steam_*` line → `Get-CoopLinkReasonText` sentence.
- The status loop shows the transport, route, RTT and persona name (§11).

### 10.2 F9: Partner / Co-op → Online (native panel, play mode only)

**Host:**
- status (state, peer persona name, route, RTT);
- `Accept joins` toggle (`accepting`);
- `Copy join code` (builds MGS2 in memory → clipboard; never displayed unmasked unless `Reveal` is held);
- an **Invite** list: friends with relationship 3, those playing app 287700 first
  (`GetFriendGamePlayed`, gameID & 0xFFFFFF; buffer ≥ 32 bytes), each with an `Invite` button →
  `InviteUserToGame(id, "+mgscoop_join " + code)`, at most once per friend per 10 s; the invited ID is
  added to the poll list.

**Guest:**
- waiting / rejected status;
- a `Paste code` box + `Join` → `ArmSteamGuest`;
- the hint "or accept the host's invite (Shift+Tab → chat)".

All Steam work is posted to the owner mailbox (§10.3). The UI never calls Steam itself.

### 10.3 Owner context (`steam_online::OwnerPass`)

`OwnerPass` runs in `observed_callbacks` after the real RunCallbacks.
- It takes `TryAcquireSRWLockExclusive` (so only one of the two callback threads runs it) and spends at
  most 4 Steam calls per pass.
- Requests: acquisition (§5), persona of the peer, friend-list page (only while the Invite page is open,
  ≤ every 5 s), invite, beacon set/clear, beacon poll.
- Results are published through a seqlock.
- If the hook is unavailable (the observer could not wrap RunCallbacks), it refuses
  `steam=disabled; reason=owner_unavailable`.

### 10.4 Rich-presence beacon (callback-free fallback; friends only)

- **Guest:** connecting for more than 10 s without a WELCOME → `SetRichPresence("mgscoop_join",
  <host account decimal>)`. On connected, rejected or shutdown → `SetRichPresence("mgscoop_join", "")`.
- **Host:** while `accepting` and not connected, every 5 s, at most 4 friends per pass: for friends in
  287700, `GetFriendRichPresence(f, "mgscoop_join") == self account` → poll-accept f (`via=beacon`).

The value only says "this player is joining that account", and only friends can see it.

## 11. Logs and `link-status.json`

**script-observer.log** (new lines; SteamIDs masked):

```
steam=acquire; logged_on=1; self=steam:…1234; messages=ok; rtti=ok; shape=forwarder; utils=ok
steam=disabled; reason=offline|self_mismatch|peer_is_self|api|not_initialized|client_module|client_iface|iface_missing|iface_module|iface_rtti|iface_shape|owner_unavailable|fault
link=config; mode=play; role=host; proto=2; bind=steam; port=0; peer=learned|waiting|steam:…1234; auth=code|pending; interp=auto
steam=accept; peer=steam:…5678; via=callback|poll|beacon
steam=session; peer=steam:…5678; state=connected; route=relay
steam=invite; to=steam:…5678; ok=1
steam=join; via=invite|code; host=steam:…1234        (or join_refused; reason=…)
steam=stats; tx=…; rx=…; ring_full=…; send_fail=…; foreign=…; bad_identity=…; accepted=…; closed_idle=…; release=…
```

`early.log` gets `steam_callbacks=registered; ids=…` or `steam_callbacks=refused; reason=…`.

**link-status.json** stays `"v":1`, with keys appended before `time_utc`:

```
"transport":"udp|steam","route":"relay|direct|unknown|none","accepting":0|1,"steam":"ok|<reason>"
```

`state` gains `waiting`. The UDP `peer` format is unchanged; for steam, `peer` is `"steam:…1234"` and
`local_port` is 0. The buffer grows from 640 to 768 bytes.

## 12. Static contract (`tools/generate_steam_shim_contract.py`)

- **Add `--check`:** regenerate in memory, compare with the committed header, exit 1 on any difference,
  write nothing.
- **New gates emitted into `steam_shim_contract.hpp`:**
  - every §1.2 export used: SteamAPI_Init 0x5420; SteamClient 0x5D90; GetHSteamUser/Pipe 0x5270/0x5260;
    GetISteamGenericInterface 0x1450; BLoggedOn 0x1950; GetSteamID 0x2030; the Friends wrappers
    0x10A0, 0x1300, 0x1C80, 0x16C0, 0x1680, 0x1D90, 0x13E0, 0x15D0, 0x14B0;
  - each with decoded-meaning asserts (slot offsets, hidden return);
  - the RunCallbacks → 0x2BA0 → 0x3320 chain and the reentrancy byte;
  - the dispatch: no `[vt+10h]` call in 0x2DE0..0x2DF9.
- **New exe asserts:**
  - the exact RegisterCallback ID set {152, 163, 331, 333, 1101, 1102, 1103, 1202, 1203} at exactly 9
    sites; fail if 337, 1251, 1252, 1221 or 1281 appear;
  - exactly one Init site (0x1428) and one RunCallbacks site (0x7DED5D);
  - the 13 Steam imports;
  - the game's Friends slot set ⊆ {0, 3, 4, 7, 29, 37};
  - no `SteamNetworkingMessages`, `…Sockets` or `…Utils` strings.
- **`--steamclient PATH` structural mode** (no header output; the coordinator runs it before any hidden or
  live Steam run). By RTTI name and strings, never by RVA, it asserts:
  - the §1.3 adapters, with their slot counts (Messages002 = 6), the forwarder shapes and the inner name
    strings;
  - the GetSessionConnectionInfo `memset` of 0x2B8, the 0x78 status and state = info+0xB0;
  - AllocateMessage's +0xB8 / +0xB0 / +0xC0 stores;
  - CreateSocketPair's argument use;
  - the interface table entry.

  It prints the steamclient hash. The runtime gates of §5 check the same properties.

## 13. Fixture tests (CTest)

**New executable `steam_transport_tests`** (with fakes). Add it to `Get-WorkerTests` in
`tools/worker/Invoke-WorkerChecks.ps1` and to the inventory in `tests/worker_checks_tests.ps1`.

Fakes:
- `FakeMessages002`, `FakeSockets012` and `FakeUtils004` implement the ABI classes;
- `FakeNet` adds loss, latency, reordering and duplication, keyed by identity, plus per-message
  `Release` counters;
- a fake module provider gives image ranges, RTTI blobs and forwarder byte arrays copied from §1.3.

Cases:
1. Identity build and check: type, cbSize, individual, zero fill; reject type 1/0/17 and cbSize ≠ 8.
2. `Release` exactly once for every message, including the rejected ones (oversize, zero, foreign,
   bad identity).
3. Guest exact filter, and the foreign-close rule.
4. Send ring: order, full → `ring_full`, size limits, flags == 0x21, channel == 0x4D47.
5. Host accept: queue cap (16), rate (4/s), non-peer cap (8), idle close (15 s) plus the 60 s deny,
   poll-accept of known IDs, `accepting=off`.
6. **Two link v2 sessions end to end over two `SteamTransport`s on `FakeNet`:** handshake, 500 FRAMEs,
   64 reliable messages on channel 2, a DATAGRAM, a guest reconnect after a broken session (the fake
   returns 3 without the flag and restarts with 0x21), `rebound` refused across a SteamID change without
   the secret, and a wrong code → verified REJECT.
7. Route decode: state cross-check, Relayed bit, mismatch → `unknown`.
8. Gates: each §5 reason, using a null interface, an out-of-module vptr, a wrong RTTI name, a bad
   forwarder, BLoggedOn false, a self mismatch, and a fault (the fake raises
   `EXCEPTION_ACCESS_VIOLATION` → latch → no further calls).
9. Callback objects: layout (+8, +0xC), the vtable slots, and dispatch the way 0x2DF7 does it
   (`call [vt+8]` with the flags test). The 1251 handler enqueues; the 337 mailbox coalesces.
10. Parsers: MGS2 vectors (shared with PowerShell), the 337 parser (prefix, NUL, trailing bytes, host ≠
    friend, oversize), and the S3 command-line extractor.
11. Shutdown: one CLOSE, one `CloseSessionWithUser`, then zero calls.

**Extend existing tests:**
- `lab_config_tests` and `lab_ini_check`: the keys and refusals of §9.1.
- `steam_shim_patches_tests`: the Init wrapper. It is installed only for play + steam, registers once
  with the right IDs and only on a true return, refuses on export-byte mismatch, and leaves other modes
  untouched.
- `lab_status_tests`: the new status keys.
- `friend_package_tests.ps1`: MGS2 vectors, steam lab.ini text, Collect-Logs redaction.

## 14. Hidden-worker self-test (coordinator runs it; lab only)

**Trigger:** lab.ini `steam_selftest=1|2`, an instance **without** `self_steamid` and `loopback` (the
worker host), at least 10 s after the observer installs. The worker guest has the lab's fake identity
and must refuse (`reason=fake_identity`). The test runs once on its own thread with a 20 s cap.
Acquisition runs in the owner context.

| Stage | Content | Pass |
|---|---|---|
| G | gates 1–7 (logged_on is recorded but not required) plus Sockets012 and Utils004 with module and RTTI checks; `GetRelayNetworkStatus(nullptr)` value recorded | all interfaces `ok` |
| P | `CreateSocketPair(&a, &b, false, nullptr, nullptr)`; two `SteamTransport`s on the **Connection leg**; host and guest `link::Session` with a random secret; handshake, 500 FRAMEs, 64 reliable messages on channel 2; then `CloseConnection` for both | handshake < 2 s; frames and reliable messages all delivered; `release == received`; identity type 1, cbSize 18 |
| L | the same with `bUseNetworkLoopback=true`, **only with `steam_selftest=2`** (C6) | as P |
| M | `ReceiveMessagesOnChannel(0x4D47)` = 0; `GetSessionConnectionInfo(self, info, nullptr)` = 0 with zeroed info; `AcceptSessionWithUser(self)` = false. May start Steam's own networking bootstrap; no peer traffic | as stated |
| T | the observer's networking-getter receipts at the game's accept/close/read return sites unchanged across the test; no 1251/337 registration in the lab; `callback_count` still rising | unchanged |

It never calls `SendMessageToUser` or `CheckPingDataUpToDate`.

**Output:**

`script-observer.log`:
```
steam_selftest=start|stage; …|done; verdict=pass|fail|refused; reason=…
```

Worker `state.json` member (`worker_debug::FormatSteamJson`):
```
"steam":{"selftest":"pass","stage":"done","api":1,"client":1,"logged_on":1,"messages":1,"sockets":1,"utils":1,
 "rtti":1,"shape":1,"relay":100,"pair":{"hs_ms":4,"frames":500,"frames_rx":500,"rel":64,"rel_rx":64,"rtt_ms":1,
 "release":564,"leak":0},"lpair":null,"inert":{"recv":0,"state":0,"accept":0},"trip":{"getter_delta":0},"reason":""}
```

There is also a read-only debug command `steam_status`, handled like `world_status`, that returns the
line.

**Scenario `tools/worker/scenarios/steam-selftest.txt`:**

```
labsetting steam_selftest=1
title 200
wait 4000
waitgameplay host 120
wait 20000
state
host run steam_status
host expect ^steam_selftest;verdict=pass;
guest run steam_status
guest expect ^steam_selftest;verdict=refused;reason=fake_identity
```

This needs no new build flag: the code is lab-gated, and the play refusal is bit 0x40000. Coordinator
run order:
1. `generate_steam_shim_contract.py --check`;
2. `--steamclient`;
3. the worker checks;
4. this scenario.

Agents never run it.

## 15. Two-PC acceptance (the user and a friend, real accounts that both own MGSV)

The self-test cannot prove the following:

1. **S1 code join between non-friends.** This proves 1251 delivery through the 2015 dispatcher
   (`via=callback`) and SDR reachability for app 287700.
2. **S2 invite with the guest waiting.** This proves 337 (`via=invite`) and poll-accept (`via=poll`).
3. **The beacon path,** forced by starting the host after the guest connects.
4. **Route:** relay and direct, depending on each player's Steam "IP sharing" setting.
5. **RTT and `interp=auto` behavior.**
6. **Zero receipts at the game's 1202 accept path** (getter returns 0x1A5F74F / 0x1A5F75E).
7. **Wi-Fi drop and recovery** (AutoRestart).
8. **Refusal when Steam is offline.**
9. **Wrong-code REJECT.**
10. **Windows Firewall prompt behavior** (Allow and Cancel both still connect).
11. **A 30-minute world-sync session.**
12. **A clean exit on both PCs:** the processes end, there is no hang, and the real saves are unchanged.
13. **S3 command-line launch,** only if approved.

## 16. Friend package and docs

- **`tools/CoopPlay.psm1`:**
  - `New-CoopSteamJoinCode`, `ConvertFrom-CoopSteamJoinCode`, `Get-CoopSteamCodeProblem`;
  - `Get-CoopSteamId64` (from ActiveUser);
  - `New-CoopPlayLabIniText -Transport steam -SteamSelf -SteamPeer -SteamWait` (the UDP output is
    byte-identical when not used);
  - `Get-CoopLinkReasonText` entries for every `steam_*` reason and `no_peer_steam`;
  - `Format-CoopStatusLine` shows transport, route and `waiting`;
  - `Get-CoopLaunchEvidence` accepts `bind=steam`.
- **`tools/friend/Play-Coop.ps1`:** the §10.1 menu and the `-Role Host|Join -Transport steam|udp
  -Code <c>` parameters. The steam flow has no UPnP, public-IP or firewall steps.
- **`Collect-Logs.ps1`:**
  - the redaction regex becomes `(?i)MGS[12][\s-]*(?:[0-9A-Z][\s-]*){22}(?:@\S+)?`;
  - also redact `\+mgscoop_join\s+\S+`;
  - `steam:…NNNN` is already masked;
  - `steam-join.ini` is never collected.
- **`FRIEND-GUIDE.txt`:** Host online / Join online, invites (start Play-Coop first), friends vs. codes,
  what to do on a firewall prompt, and the LAN/VPN fallback.
- **Install-Coop / Uninstall-Coop:** only the S3 consent file.
- **Docs:**
  - `docs/steam-online.md` (this spec);
  - `docs/two-pc-play.md`: a new "Online (Steam)" section, with UDP kept as LAN/VPN;
  - `docs/link-protocol.md` §1.1 keys, §1.2 bits, a new §2b "Transport (`lab::SteamTransport`)", §3
    (the DLL now parses MGS2), §7 lines and status keys;
  - `docs/STATUS.md` entry, README one-liner.

## 17. Risks and open questions

| Risk | Mitigation |
|---|---|
| R1. 1251 or 337 not delivered through the 2015 steam_api (the dispatch path looks generic) | poll-accept for known IDs; beacon for friends; codes between non-friends then need 1251 (two-PC item 1) |
| R2. A Steam update changes adapters or RTTI | refusal (gate 7) → LAN/VPN code; `--steamclient` reruns on the new build |
| R3. The unlocked callback multimap | register only in the Init wrapper, before any game thread exists |
| R4. GNS global-lock contention with the game | in play the game's legacy P2P is idle (offline); the send ring keeps game threads out; measure `send_us_max` |
| R5. SDR availability or permission for app 287700 | expected to be available to all apps; unproven (two-PC item 1) |
| R6. IP exposure on direct routes | Steam's "IP sharing" user setting governs it; we do not override the ICE config |
| R7. A fault inside steamclient | SEH latch; the process may need a restart; logged |
| R8. Exit hang | park on the shutdown flag (C11); two-PC item 12 |
| R9. Invite chat history keeps the code | per-session secret; it dies with the session |
| R10. Hidden self-test touches Valve infrastructure (stage M) | Steam traffic only; no peer, game or Konami traffic; stage L opt-in |
| R11. S3 command-line parsing and visibility of the secret | S3 behind consent and a user decision |

## 18. Work packages (order)

| Package | Content |
|---|---|
| **WP-A** | ABI header, SteamTransport + legs, `steam_online` acquisition, gates and owner pass, self-test + `state.json` + scenario, contract `--check` / `--steamclient`, `steam_transport_tests` + inventory. **The coordinator runs the hidden self-test** |
| **WP-B (S1)** | lab_config keys and bits, Init wrapper + 1251, accept policy, `partner_net` selection and status, CoopPlay / Play-Coop Host online and Join online with a code, Collect-Logs, docs. → user/friend two-PC items 1, 4–12 |
| **WP-C (S2)** | F9 Online panel, `InviteUserToGame`, 337 + waiting guest + `ArmSteamGuest`, rich-presence beacon. → items 2–3 |
| **WP-D (S3)** | only after a user decision |

Every package: CRLF native, Lua and test files; `/W4 /WX`; `noexcept`; constant-initialized state; no
allocation after Start; local commits only.


## 19. Implementation notes (how the code follows this design)

Files:

| Piece | Files |
|---|---|
| ABI (pure) | `native/include/mgscoop/steam_net_abi.hpp` |
| MGS2 / 337 / S3 parsers (pure, `mgs_coop_core`) | `native/steam_join_code.cpp`, `native/include/mgscoop/steam_join_code.hpp` |
| Structural gates (pure) | `native/steam_net_gates.cpp`, `native/include/mgscoop/steam_net_gates.hpp` |
| Callback objects 1251/337 | `native/steam_net_callbacks.cpp` |
| Transport, send ring, accept policy, guest filter | `native/steam_transport.cpp` |
| Link v2 test end (self-test, fixtures) | `native/steam_link_harness.cpp` |
| Owner context, acquisition, F9 requests, beacon, self-test | `native/steam_online.cpp` (in the loader DLL) |
| UI mailbox and self-test report | `native/steam_online_state.cpp` (`mgs_debug_menu`) |
| Init wrapper | `native/early_patches.cpp` (`InitWrapperRefusal`, `RunInitWrapper`) |
| partner_net integration | `native/partner_net.cpp` (`StartSteamLink`, `ArmSteamGuest`, `SteamGuestIdle`) |
| F9 Online panel | `native/debug_menu_ui.cpp` (`Online()`) |
| Contract | `tools/generate_steam_shim_contract.py` (`--check`, `--steamclient`) |
| Tests | `tests/steam_transport_tests.cpp` (new), `lab_config_tests`, `lab_ini_check`, `lab_status_tests`, `steam_shim_patches_tests`, `worker_debug_tests`, `tests/friend_package_tests.ps1` |
| Self-test scenario | `tools/worker/scenarios/steam-selftest.txt` |

Decisions and deviations:

1. **Masked SteamIDs are ASCII**: `steam:...1234` (the last four digits of
   the SteamID64), because logs and `link-status.json` are ASCII.
2. **A wrong code is a hint, not a verified REJECT** (section 13 case 6).
   Link v2 answers a HELLO with a bad tag with an unauthenticated
   `REJECT(bad_code)` (at most one per second); the guest reports it with
   `verified=0` and keeps connecting. The fixture asserts that hint and that
   no session forms. The verified REJECT across Steam is `busy` (a second
   guest with the right code while the first is live). A pasted F9 code may
   therefore replace a code that drew a `bad_code` hint while the guest is
   still connecting (337 invites stay limited to waiting or rejected guests).
3. **Self-test trigger**: the worker host has `loopback=1` and no
   `self_steamid`; "an instance without `self_steamid` and `loopback`" is read
   as that host. Any lab instance with `self_steamid` (the worker guest)
   refuses with `fake_identity` and makes no Steam call.
4. **`accepting=off`** refuses new identities (callback and poll), but the
   current and last session peer are still poll-accepted, so an existing
   partner can recover a broken Steam session.
5. **The relay warm-up** (`CheckPingDataUpToDate(1e10f)`) runs on the
   transport thread when it becomes ready (`steam=relay; warm=...`), keeping
   every Utils call off the owner context (section 2).
6. **The host beacon** uses the friend list of the F9 Invite page when it is
   open, otherwise a slow background scan of in-game friends at most every
   60 s while accepting and not connected (at most 4 Steam calls per pass);
   then at most 4 `GetFriendRichPresence` per pass, every 5 s.
7. **337 mailbox**: a request less than 2 s after the last stored one is
   dropped (counted `join_throttled`); later ones replace the slot.
8. **`set_peer`** may be called from any thread: it posts the peer to the
   transport thread, which applies it to the accept policy.
9. **link-status.json on UDP** carries the new keys too:
   `"transport":"udp","route":"none","accepting":0,"steam":"off"`.
10. **`steam_status`** is answered natively from the popped command (like a
    read-only report); it is never dispatched to Lua.
11. **S3** is not wired: `steam_code::ExtractCommandLineJoin` and its tests
    exist; DllMain, Install-Coop and Uninstall-Coop are unchanged until the
    user decides.
12. **Play-Coop** keeps `-Role` without `-Transport` on the LAN/VPN flow
    (byte-identical); the menu's `[L]` leads to the old Host/Join prompt.
    `-Code` exists for scripted use; the prompt keeps the code out of process
    lists. A waiting guest gets no `link-secret.txt`.

Review fixes (2026-10-08, after the first implementation):

13. **Poll-accept and the rate limit** (6.3): polls of host-chosen ids
    (`SteamAcceptPolicy::CheckKnown`) no longer consume the 4/s rate tokens.
    Before, one invited friend who was not joining used all four tokens every
    second (and four or more used them at the start of every window), so a
    code join's single 1251 was refused (`accept_refused`) and, Steam
    re-posting it only periodically, the joiner could stay unaccepted.
14. **Established sessions answer the poll with true** (6.3, verified in
    steamclient `caba4826...` at 0xA7AEF0): the transport marks live ids and
    counts/logs `steam=accept` only for a new session. Before, the host logged
    `steam=accept; via=poll` 4 times a second for the whole session. The
    fixture's fake now has the real semantics.
15. **337 handler lock** (6.2): the payload is copied before the lock.
    Before, a fault while reading it left the SRW lock held (the game's
    callback thread and the transport thread would block forever).
16. **Init wrapper** (6.1) also requires the save redirect; the F9 paste
    reports "Steam networking is off" instead of "joining" when the Steam
    path is disabled; the shutdown waits 30 ms before
    `CloseSessionWithUser` (7.6); Play-Coop's MGS2 parser uses regex escapes
    instead of raw line breaks inside a string.

Verification of this implementation (fixture only):

- `steam_transport_tests`: 393 checks, including two link v2 sessions end to
  end over two `SteamTransport`s on a fake Messages002 hub with latency,
  reordering and duplication (handshake, 500 FRAMEs, 64 reliable messages,
  a DATAGRAM, route, a broken Steam session restarted by 0x21 and
  re-accepted, busy REJECT, wrong-code hint, release == received), the
  connection leg with IP-localhost identities, the gates on synthetic PE
  images, callback dispatch as at 0x2DF7, a fault latch, polls that leave
  the rate tokens to a code join, one `steam=accept` per new session, and a
  337 payload fault that leaves no lock held.
- `generate_steam_shim_contract.py --check` and `--steamclient` pass on
  `mgsvtpp.exe 085c2f82...`, `steam_api64.dll 08bd0152...` and
  `steamclient64.dll caba4826...`.

## 20. Code joins without the 2015 callback manager, and the online UX

**Status (2026-10-09, `wp/steam-join`): implemented, fixture-tested and
statically contracted (`--steamclient`); never run in the game.** The user's
request: "code joining ... make everything nice and neat for online".

### 20.1 Problem

The host learns of a joiner it does not already know only from callback
1251, registered through the 2015 steam_api's `RegisterCallback` by the Init
wrapper (6.1). Whether that dispatcher delivers it is unproven (R1). Polling
covers only ids the host already knows (the peer, invited friends, beacon
hits), so a code join between players who are not Steam friends depended on
that one path.

### 20.2 Mechanism (steamclient64 `caba4826...`, asserted by `--steamclient`)

GNS function-pointer callbacks: a config value of type Ptr names a function
that steamclient queues the callback for; `ISteamNetworkingSockets012::
RunCallbacks()` then calls it on the calling thread. Every row is asserted by
`tools/generate_steam_shim_contract.py --steamclient` by RTTI, name strings
and instruction shapes, never by RVA (ten binary mutations each fail it):

| Fact | Asserted |
|---|---|
| Sockets012 slot 41 is `RunCallbacks` | the gate's disp32 forwarder to inner slot 41 (the steamclient class keeps the base slot); assert string `Unknown callback type %d!`; lock `this+0x878`, swap of the queue vector `this+0x860`, exactly two unlock calls, then the loop: `call [entry+8]` with `rcx = entry+0x10` (the payload), 0x2D8-byte entries; the jump table reaches that call for exactly 1221, 1222, 1223, 1251, 1252 and 1281 (`kRunCallbacksIds`) |
| Utils004 slot 16 is `SetConfigValue(value, scope, obj, type, pArg)` | the gate's disp32 forwarder; name string; Ptr: type 5 (5th argument) stores `*(void**)pArg` (6th) and marks it set; `pArg == nullptr` resets the Global value to its default |
| The config entries | 201 `Callback_ConnectionStatusChanged` (Ptr, Connection scope); 204 `Callback_MessagesSessionRequest` and 205 `Callback_MessagesSessionFailed` (Ptr, Global) |
| Their readers | exactly one load each: 204 in `CSteamNetworkingMessages::BHandleNewIncomingConnection` queues 1251 (the 0x88-byte identity) on `[this+8]`, the sockets object that owns the Messages object; 205 queues 1252 (the 0x2B8-byte connection info) |
| InternalQueueCallback (base slot 68, `call [vtbl+0x220]`) | a null function queues nothing; payload <= 0x2C8; the same lock and vector as RunCallbacks; the steamclient override then always posts to the game's pipe (no branch) |
| Payloads | 1221 `{hConn @0; info @8; old state @0x2C0}` (0x2C8); the info filler writes state +0xB0, end reason +0xB4, end debug +0xB8 (0x80) |

Consequences:
- The game is unaffected: the pipe post of 1251/1252 still happens and the
  game registers neither id (generator census); the game's
  ISteamNetworking005 and 1202/1203 never read these values.
- Our functions run only inside our own `RunCallbacks` call, on our thread,
  outside every Steam lock. Nothing else in the process calls this
  `RunCallbacks` (mgsvtpp, steam_api64 2015 and the overlay hold no GNS
  interface string).
- The Init wrapper's 1251 object stays a second source. Section 2's "never
  call `RunCallbacks`" is about `SteamAPI_RunCallbacks`; the GNS
  `RunCallbacks` drains only the queue our function pointers fill.

### 20.3 Implementation

- `native/steam_net_dispatch.{hpp,cpp}`: `Install` (204 + 205 on the host, 205
  on the guest; all or nothing), `Pump` and `Uninstall` through C-style SEH
  thunks. The functions copy the payload first, then only enqueue (1251: the
  `steam_cb` queue with a source mask; 1252: a newest-wins mailbox; 1221: the
  self-test record). They never block on a game lock, log or call Steam.
- `SteamTransport`: installs once ready, pumps every 10 ms between the
  receive and the accept work (a code join is accepted in the same pass),
  resets the values on `stop()` and on the window-close shutdown before
  `CloseSessionWithUser`. After a fault no Steam call follows, so the values
  may stay set; only incoming session requests then fill an undrained queue.
- Logs: `steam=dispatch; fnptr=on; ids=1251+1252; pump_ms=10` or
  `fnptr=refused; reason=...; fallback=callback+poll`;
  `steam=accept; peer=...; via=fnptr|callback|fnptr+callback|poll|beacon` (the
  other source reporting the same accept within 5 s counts `dup`);
  `steam=stats` gains `req_cb`, `req_fn`, `dup`, `failed`, `pumps` and
  `dispatch`.

### 20.4 Runtime gate and refusals

The owner context acquires Sockets012 in play too when the variant uses the
function pointer, then runs a call-free gate (`DispatchGate`): Sockets012
slot 41 and Utils004 slot 16 forward to the expected inner classes
(`.?AVCSteamNetworkingSocketsSteamClient@...`,
`.?AVCSteamNetworkingUtilsSteamClient@...`, slot in `.text`), and the
Messages implementation's owning sockets object (`[subobject - 0xD0 + 8]`) is
the Sockets012 adapter's inner object. Any failure refuses only the function
pointer (`steam=dispatch_gate; check=...; reason=...`); play continues on the
1251 callback and polling. Nothing here ever refuses play.

### 20.5 Variants (the two-PC A/B)

lab.ini `net_steam_dispatch` (play; written by Play-Coop `-SteamDispatch`, a
hidden developer parameter; no key = `both`):

| Value | Function pointer | Init wrapper (1251 host / 337 guest) | Poll / beacon |
|---|---|---|---|
| `both` (default) | host 204+205, guest 205 | yes | yes |
| `fnptr` | host 204+205, guest 205 | guest 337 only (invites need it) | yes |
| `callback` | no (the behaviour before this section) | yes | yes |
| `poll` | no | no | yes |

early.log says `steam_callbacks=skipped; reason=dispatch_fnptr|dispatch_poll`
when the wrapper is not wanted.

### 20.6 Self-test stage F/D (lab; `steam_selftest_fn`, default 1)

After P/L and before M, within the 20 s cap, on the self-test thread:
- **F1**: `SetConfigValue(204, Global, Ptr, &fn)`, then the reset: both true.
- **F2**: `CreateSocketPair`, 201 on both ends (Connection scope), close b;
  our 1221 calls before our first pump must be 0; pump up to 1 s until a call
  for hConn a with old 3 (Connected) and new 4|5, on this thread; close a,
  one more pump.
- **D**: the play path: `Install(204+205)`, `Pump` every 10 ms for 2 s,
  `Uninstall`, and nothing installed afterwards.

Pass: state.json `steam.fn.verdict` `"ok"`, and the `steam_status` line ends
`;fn=ok` (`fn=skip` with `steam_selftest_fn=0`). This proves the queue ->
RunCallbacks -> our function path on one account; the 204 -> 1251 delivery
itself needs two accounts (section 15 item 1, `req_fn`).

### 20.7 Why a join failed (1252) and plain-language reasons

The 205 function gives the guest Steam's end reason:
`steam=session_failed; peer=...; end=<ESteamNetConnectionEnd>; state=...;
debug="..."` (printable ASCII, dotted IPv4 masked `a.b.x.x`). For the guest's
configured host while not connected it becomes link-status `"steam_end":<n>`,
the in-game line and a toast; a connect clears it. `native/coop_reason_text.cpp`
and `tools/CoopPlay.psm1` (`Get-CoopLinkReasonText`, `Get-CoopSteamEndText`)
produce the same sentences; `tests/fixtures/link-reasons.json` holds the
shared vectors that `steam_transport_tests` and `friend_package_tests` both
check. The end-reason table uses the public SDK values (their names are not
in the binary): 5003/4001 "the host's game did not answer", 5008/5009 "Steam
could not connect the two PCs", 5005/3001/3002 "this PC cannot reach Steam's
network", 3004/3005 "restart Steam", 4002/4003 "secure handshake failed",
4006 "different Steam version", 1000-1999 and 5010 "the host closed the
connection", otherwise "the Steam connection failed (code N)".

### 20.8 Partner left

A Steam transport sends one link CLOSE at the window-close shutdown (7.6; UDP
never sends one). An authenticated CLOSE makes the host `lost` at once
(`link=peer_left; role=host; next=accepting`): a late, reordered DATA of the
closed session no longer revives it, and the next join (even from another
SteamID) is accepted at once instead of after 3 s. The guest drops the closed
session (no keepalives into it) and starts reconnecting. link-status
`"left":1` until the next session; Play-Coop's status line adds "your friend
left" / "the host left" while not connected.

### 20.9 In-game status line and toasts (`coop_hud`)

The overlay draws one line top-right while F9 is closed (hidden during
demos), for example `CO-OP [connected] Snake_Fan - Steam relay 61 ms`,
`CO-OP [waiting] code ready - F9 to copy/invite`, `CO-OP [joining 12 s] host
not answering` or `CO-OP [refused] wrong code?`; `full` adds a detail line
(rtt, jitter, loss, build, peer). Modes `compact|full|off`: lab.ini
`coop_hud` wins, then coop.ini `hud` (play only); default compact in play and
off in the one-PC lab. Toasts (4 s, at most 3): connected, reconnected, lost,
partner left, refused (the plain reason), version mismatch, Steam
unavailable, the 1252 end reason, joining, invalid join code, invite sent and
code copied (those two show for what is left of their 4 s once F9 closes).
Nothing reads game memory or calls Steam. Worker evidence: state.json `hud`
(`enabled, mode, frames, hidden, events, toasts, line, toast, tick_ms`) and
the read-only debug command `hud_status`
(`hud;mode=...;on=...;frames=...;line=...;toast=...`).

### 20.10 Build id

CMake makes the link build (HELLO/WELCOME, early.log and probe.log version)
the version plus the 7-character commit at configure time, for example
`0.11.1+2459c8c`: two packages built from different commits refuse each other
with `build_mismatch` and the "both must install the same package" sentence.
Play-Coop's own version text stays the package version.

### 20.11 Runs and evidence (coordinator)

Static first: `python tools/generate_steam_shim_contract.py --check` and
`--steamclient "C:/Program Files (x86)/Steam/steamclient64.dll"` (its line
must end with the `dispatch: ...: ok` part), then the worker checks.

1. `pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/steam-selftest.txt`:
   host `steam_status` `verdict=pass;...;fn=ok`; `roles.host.steam.fn`:
   `verdict "ok"`, `set204 1`, `reset204 1`, `pre_pump 0`, `conn_ok 1`,
   `old 3`, `new 4|5`, `thread_ok 1`, `d_install 1`, `d_uninstall 1`,
   `faults 0`; script-observer.log `steam_dispatch=selftest; gate=ok; ...`.
2. The same with `-LabSetting steam_selftest_fn=0`: `fn=skip`, verdict pass
   (only the host expectation of the scenario fails, by design).
3. `pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/coop-hud.txt`:
   both `hud_status` `on=1;frames=<n>;line=CO-OP [connected]...`,
   `roles.<role>.hud.enabled true`, `hud.hidden` grows while F9 is open and
   `hud.frames` again after it closes; the captures show the line. With
   `-LabSetting coop_hud=off`: `on=0`, no `overlay=hud_drawn` line.
4. Lab regression: any existing scenario without these keys draws no HUD
   (`hud.enabled false`) and logs no `steam_dispatch` or `steam=dispatch` line.

Two-PC (the user and a friend, section 15): an S1 code join between
non-friends with the default `both`: host `steam=accept; ...; via=fnptr` or
`fnptr+callback` and `steam=stats` `req_fn >= 1`. R1 is settled by
`req_cb`: 0 means the 2015 dispatcher never delivered 1251. Separate the
sources with `-SteamDispatch callback` and `-SteamDispatch fnptr`. A guest
whose host is not running sees `steam=session_failed; end=5003` and the
sentence; the host quitting shows "the host left" at once on the guest.

### 20.12 Review fixes (2026-10-09)

- `--steamclient` now asserts every steamclient property section 20 relies on
  (20.2); before, only the runtime RTTI/forwarder gate covered slots 41/16.
- A late DATA after a CLOSE revived the host (`lost -> connected`, clearing
  "left") or the guest; the guest kept sending keepalives into the closed
  session. Fixed in `link::Session::OnPeerClose`; the fixture delivers held
  DATA after the CLOSE.
- The CLOSE packet's own lost/reconnecting events were toasted as "Lost the
  connection" before "left": the goodbye flag is now set first.
- The 1252 debug text is logged and collected: IPv4 addresses are masked.
- `steam_transport_tests` took the vector file as a command argument, which
  the audited worker checks refuse; the path is a compile definition now.
- The new lab keys were neither in the worker `-LabSetting` allow-lists nor in
  LabCommon's lab.ini allow-list (a hidden run with them would have failed);
  both are added and `worker_session_tests` asserts the two allow-lists stay
  identical.
- The invite-sent and code-copied toasts were never posted; the self-test's
  pair end b is closed on every non-fault path.

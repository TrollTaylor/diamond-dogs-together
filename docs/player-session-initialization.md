# Player initialization consumes network mode

The installed executable contains an early player-construction decision that affects subsequent allocation. This supports retaining early session/online setup as the implementation candidate rather than moving host creation to the later point where the four-slot readiness diagnostic passes.

The new original analyzer verifies 46 instruction predicates, one allocation-method table entry, and four complete exception-table bodies on executable SHA-256 `085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45`. No game call, process-memory write or hook is performed.

## Verified dataflow

Constructor `0x9ba700` installs the primary table `0x22953d0` and secondary table `0x2295490` at enclosing object plus `0x20`. This binds the secondary view to the same table observed by the existing player reader.

At `0x9baa2b`, it loads the application service member at `+0xf8`, invokes its first method with byte argument `0x14`, and, if the result is true, sets bit 8 at secondary `+0x204` (enclosing `+0x224`). The pinned source names this application member `gameStatusInterface` and status index 20 `S_IS_ONLINE`. Those names are source interpretation; this analysis independently checks the argument, receiver path, branch and bit write, not the concrete virtual target or status setter.

The primary table's first entry is allocation wrapper `0x9bd7a0`, which calls `0x9bd7e0`. Its complete body spans 5,221 bytes; the generic helper's 4,096-byte cap was not used for final verification. Allocation reads the same bit 8 and obtains an interface from the QuarkSystemTable network-service candidate (`+0x78`, method `+0x40`). With bit 8 set, it:

- Clamps the requested index limit using interface method `+0x20`.
- Performs network-service method `+0x28` setup and stores the result at enclosing `+0x178`.
- Reads interface methods corresponding in pinned source to local-member/index and host information, storing a local index at enclosing `+0x234`.
- Creates an additional component under identifier `0x6593a4717b40`, storing its adjusted interface at enclosing `+0xc8`.

The bit-clear branches skip these operations. Four reserved player slots alone do not recreate them. The concrete virtual targets and the conditional component's identity are not established by this report, so no new calls are enabled.

The preceding Mother Base sample in PID 14496 recorded secondary flags `230535` (`0x38487`), with bit 8 clear, while its index limit was four. This is an interpretation of the preserved 0.9.6 sample, not a fresh gameplay observation. It agrees with the experiment's disabled host/online setup. The pause menu's online label is not evidence of this per-player construction bit.

## Code correction and remaining work

Prepared loader 0.9.7 separates `HostPrerequisites` from `PlayerReadiness`. Early prerequisites retain mission, target type/state, executable and allocation-context checks. A null player is an explicit later-readiness result rather than a prerequisite for creating the early session. The unconditional allocation-lifetime refusal still prevents every native host call. This change does not mark a mission online or accept peers.

All 14 native suites pass. These existing suites verify build/integration regressions; they do not simulate game readiness or prove a host lifecycle. Lua insertion is unchanged from the 0.9.6 trial. Version 0.9.7 is built but **not installed**; the game remains PID 14496 at Continue with installed 0.9.6.

Next finish the identified persistent network allocations and asynchronous release path. Retain early session/online setup for the controlled trial, with later actor validation as its own stage. A supported late reinitialization path has not been ruled out exhaustively, but none has been established and a later successful readiness check is insufficient justification for one.

Reproduce:

```powershell
C:/Python314/python.exe tools/analyze_player_session_setup.py --output build/player-session-setup-analysis.json
```

Evidence: [complete static analysis](evidence/player-session-setup-analysis-2026-09-09.json), [build results](evidence/player-session-prerequisites-build-2026-09-09.log), [preserved live sample](evidence/host-readiness-player-storage-2026-09-09.json), [prior timing trial](host-readiness-timing.md).

Attribution: pinned tpp-mod `301bf797b7f8032941b21ed56a3570338de6ead4`, `structs.hpp` and `symbols.hpp`; pinned Dynamite `c90f252f8200765f66c005d6bf4357fda0c0d98e`, `TppGameStatusFlag.h`. The analyzer and diagnostic correction are project-authored. [Third-party notices](../THIRD_PARTY_NOTICES.md).

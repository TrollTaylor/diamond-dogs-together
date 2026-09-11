# Live four-slot player storage

2026-09-09. The 0.8.0 Mother Base experiment now has direct evidence of **four indexed player-system storage entries**, beyond the earlier package-selection receipt. This does not establish four initialized characters or playable co-op.

The same game process, PID 41584, remained paused at Mother Base. No restart, debugger, injected query or game-memory write was used. The original [external observer](../tools/inspect_player_system.py) opens the process with query/read access, checks the executable fingerprint, compares six live instruction spans and checks the observed method tables before interpreting the descriptor or indexed storage. The root comes from the pinned tpp-mod `player2System` symbol, RVA 0x2A82C28. Attribution remains in [third-party notices](../THIRD_PARTY_NOTICES.md).

## Observed values

| Observation | Value | Interpretation |
| --- | --- | --- |
| Player-system implementation | 0x1A0A91C00 | Current process address only; not retained by the mod |
| Descriptor enabled bit | Set | Its indexed range is exposed |
| Descriptor index limit | 4 | Generic lookup accepts unsigned indices below this limit |
| Descriptor secondary value | 4 | Meaning beyond the decoded copy remains unverified |
| Indexed executor limit | 4 | Agrees with the descriptor |
| Executor bitset | 0x0F | Getter yields true for indices 0–3; broader gameplay meaning unverified |
| Indexed storage pointers | Four distinct, nonzero addresses | Each inspected 64-byte header is readable |
| Additional entries 1–3 | First 64 bytes zero | Reserved storage does not prove initialized character state |

The four storage pointers are 0x1A0A77C30, 0x1A0A72F60, 0x1A0A6E290 and 0x1A0A695C0. Their spacing is 0x4CD0. This is observed spacing, not a verified allocation size or memory budget. Pointer/descriptor/bitset rereads detect some changes but do not establish an atomic snapshot or lifetime lease. [Final bounded observation](evidence/player-allocation-four-slots-2026-09-09.json), [earlier header observation](evidence/player-system-instance-headers-2026-09-09.json).

## Static derivation

The secondary player-system method at 0x9C39F0 subtracts 0x20 from its receiver and calls the generic indexed resolver at 0xBF4270. The resolver calls the enclosing object's method at slot 0x40, then compares the requested unsigned index against descriptor DWORD +4. Only an index below that limit obtains the executor through enclosing +8, then internal +0x28.

The live enclosing method table maps that descriptor slot to 0x9C39B0. Its complete 62-byte body checks enclosing +0x224 bit zero, reads +0x228 into descriptor +4 and +0x230 into descriptor +8. Both return paths were decoded; the generic leaf decoder alone stopped at the first return and was insufficient.

The live executor maps slot +8 to 0xBF5250. Its complete body reads one bit from the bitset at executor +0x28; a null bitset returns true. The corresponding setter bounds its index using executor DWORD +0x0C. The player-system secondary method at 0x9C3BB0 indexes a pointer array at secondary +0x430. No method was called by the observer.

Reproduce the six static spans/checks and four method-slot checks with:

```powershell
python tools/analyze_player_allocation.py --output build/player-allocation-analysis.json
# Replace this historical PID with the verified current game PID.
python tools/inspect_player_system.py 41584 --output build/player-allocation-observation.json
```

[Static evidence](evidence/player-allocation-access-analysis-2026-09-09.json). Earlier exploratory header reports retain their narrower checks and are not promoted to final verification.

Next establish how the game initializes these additional slots during a host/client connection, including body, equipment, position, physics and input ownership. Then prove independent control before moving to real peer synchronization and campaign behavior. All campaign support remains unverified.

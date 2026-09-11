# Live player motion observation

On 2026-09-09, original loader 0.4.0 remained loaded in MGSV PID 106392 (started 16:41:22 UTC). Ordinary Return to Mother Base and helicopter disembarkation reached the Command Platform. A single Space input produced a visible forward dive and prone posture. A later C input recovered to crouching. The independent external reader's position and posture changed alongside these actions. **This is one local movement correlation, not an actor identity, root-transform definition, networking test or playable co-op.**

Executable SHA-256: `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`. Installed loader SHA-256 remained `58C0EE96991C905DEC4528E363E3F10A322223629EDD0677D36C446F4B590FF6`. The sampler used read-only process access, verified the executable and mapped accessor bytes, and made no calls or writes in the game.

## Results

The in-game Key Assignments screen confirmed W forward, C stance and Space quick dive. No bindings were changed. Input used the Computer Use API; the [recorded UTC intervals](evidence/player-motion-input-events-2026-09-09.json) bound tool calls, not physical key-hold duration or engine delivery time.

| Trial | Observed result | Recording |
| --- | --- | --- |
| Disembark with the visible E prompt | Snake reached the platform, crouched beside D-Dog; 10 stationary samples | [Disembark](evidence/player-motion-disembark-2026-09-09.jsonl) |
| Short `w` tap | No visible displacement established; all 300 positions and status records unchanged | [First forward trial](evidence/player-motion-forward-2026-09-09.jsonl) |
| Short `c` tap | No posture change established; 10 following samples unchanged | [First posture trial](evidence/player-motion-posture-2026-09-09.jsonl) |
| Space, 17:16:57.478–17:16:57.561 UTC | Visible dive and prone landing; published vector changed over samples 189–198 | [Dive, 300 samples](evidence/player-motion-dive-2026-09-09.jsonl) |
| `C`, 17:17:29.832–17:17:29.928 UTC | Visible recovery to crouching; 10 following samples had SQUAT true and CRAWL false | [Recovery](evidence/player-motion-recover-2026-09-09.jsonl) |
| Short `W` tap | No measurable displacement in 300 samples | [Second forward trial](evidence/player-motion-forward-uppercase-2026-09-09.jsonl) |

No conclusion about uppercase versus lowercase input follows from these few trials. Key duration, delivery and gameplay handling have not been isolated. Sustained ordinary movement and controller input remain unverified.

In the dive trace, samples 0–188 held reference position `[9.04313564,0.800121009,-40.7664185]`, SQUAT true. Samples 189–197 changed position with CRAWL true and NORMAL_ACTION false. Sample 197 also had WALK true; this alone does not classify the action as walking. Samples 198–299 held `[9.13846684,0.350120991,-35.987709]`, CRAWL and NORMAL_ACTION true. Horizontal displacement was approximately 4.78 coordinate units. Mission remained 30050 and local index remained zero.

Recovery retained X/Z but changed Y from `0.350120991` to `0.800121009`, with SQUAT true. This posture-dependent height is a reason to avoid treating the point as an unqualified feet/root transform. Its precise definition remains unresolved.

The named coordinate fields and published vector differed in five of the 300 dive samples (190, 191, 192, 194 and 196), although selector rereads passed. They also differed in 37 of 60 earlier helicopter-arrival samples. These are non-atomic reads of distinct storage; equal bank/index selectors do not synchronize publication or exclude concurrent writes. Do not combine them into a supposedly coherent replication snapshot. The [computed summary](evidence/player-motion-summary-2026-09-09.json) preserves counts, endpoints and observed value groups.

## Bounded sampling across publication changes

The [first base-transition recording](evidence/player-motion-base-transition-2026-09-09.jsonl) ended with `player_selectors_changed_during_sample`. The external reader now skips that observation, emits only its reason and sample number, sleeps for the configured interval and continues. Each skipped attempt consumes one of the finite sample slots; it neither emits mixed fields nor retries indefinitely. Other read/identity refusals remain unchanged. A separately emitted network record is not an atomic pair with the world record.

The rebuilt reader and all 13 existing CTest suites passed: [build output](evidence/native-tests-with-motion-sampling-2026-09-09.log). These existing suites do not provide new fixture coverage for the live publication race. The later live recordings completed successfully, but did not encounter another selector skip.

## Session and save checks

All seven recordings summarized above observed the session-owner raw state and flag as zero with the expected constructor table. This does not establish transport absence or connection readiness. At the post-trial log inspection, the installed networking getter observer still had no natural getter receipt. No custom Steam traffic, online battle or reward claim was initiated.

All seven save/config files matched the preserved closed-game baseline before travel: [before check](evidence/save-check-before-motion-2026-09-09.json). The [after check](evidence/save-check-after-motion-2026-09-09.json) found six still matched and `TPP_GAME_DATA1` changed, last write 17:13:15.9582552 UTC during ordinary arrival/disembarkation. Its semantic changes are unknown. No source save or backup was restored or overwritten by the tools; this is not implemented save isolation.

The game was left paused at Mother Base after these trials. Next identify the natural producer and owning actor behind the [verified published-vector setter paths](player-vector-producer-investigation.md), with lifetime and update-thread evidence. Do not call those setters to create motion or infer additional actor capacity from the record array.

# Hidden worker: first animation evaluation (2026-10-07)

Run `build/worker-runtime/904f772a2df64307a469db4ef05fface`: hidden private-desktop
session (tools/worker/Invoke-WorkerScenario.ps1, scenario
tools/worker/scenarios/partner-locomotion.txt, lab saves, animated partner profile,
Afghanistan 30010, noon). The guest filmed the host's partner body; per-capture
telemetry is in that run's `captures.jsonl`. Real saves were unchanged.

Telemetry (guest's view of the host's partner):

| Host action | mapped_clip / remote_act | Problem |
|---|---|---|
| Run (W, standing) | 1 / 3 the whole time (squat idle) | Partner slides along in a crouched idle pose: no run clip |
| Sprint (Shift+W) | 4 / 4 (squat run) | Crouched run while the host sprints upright |
| Walk (Ctrl+W) | 4 / 4 (squat run) | Same |
| Crouch (C) | 0 / 1 (stand idle) | Inverted: the partner stands when the host crouches |
| Crouch-move | 11 / 2 (stand run) | Inverted |
| Prone / crawl | 7 / 5, then 6 / 6 | Correct |
| Stationary mouse turns | 0 / 1, no turn clip | No turn clips selected |
| Strafe (A) | 11 / 2 (stand run) | Run clip while strafing sideways |

Conclusion: the standing and crouched stance classes are swapped in the clip
selection (remote_act 1/2 vs 3/4), and the standing run never leaves idle. This is
the main reason the partner looks "awful". Next: fix the stance/act mapping in the
dm=map classifier (native/partner_dmmap.cpp, remote_act source in partner_driver /
partner_net), then re-run this scenario and the combat scenario
(tools/worker/scenarios/partner-combat.txt) to compare.

# Project requirements

- Build an original MGSV: The Phantom Pain co-op mod for one to four players, including the first playable hospital sequence through the released campaign endings.
- The user subsequently prioritized direct Dynamite reuse and a fully playable two-player mode. Two players are the immediate implementation and acceptance target; four-player expansion is deferred until appropriate. Preserve the full-campaign objective, but prioritize real host/join gameplay over further generic infrastructure.
- The user explicitly deferred complete campaign scripting and progression synchronization. The immediate deliverable is two independently controlled players together in a scene with shared gameplay/combat; stage-three campaign adaptations come later.
- On 2026-09-09 the user explicitly superseded the earlier Dynamite prohibition and authorized reading its source and reusing anything needed to accelerate this project. Inspect and record the upstream license and revision before incorporating code; preserve required attribution and identify adapted code honestly. The target remains this project's full one-to-four-player campaign, not Dynamite's existing support scope.
- Reuse verified game networking and Steam functionality where appropriate through the original adapter. A lobby size or core test does not demonstrate playable remote actors.
- Keep actual gameplay support separate from development tests and static candidates. All campaign entries remain unsupported until their complete playable sequences have recorded verification.
- Preserve the installed executable, Steam DLL and saves. Diagnostic loader installation is reversible and owned by its hash record. Do not replace an existing unrelated loader.
- Do not call or patch addresses obtained only from string/table candidates. Verify executable fingerprint, calling convention, expected bytes, lifecycle and behavior first.
- Read README.md and docs/STATUS.md for current progress. Build/check with tools/build.ps1. Tool-generated reports must identify limitations and the exact inspected executable.

## Background agent development

- On October 7, 2026 the user requested development without Computer Use and without game windows appearing on their desktop. This applies to all agents working on this project.
- Do not use Computer Use, launch MGSV on the user's visible/default desktop, steal desktop focus, or inject system-wide keyboard/mouse input for development tests.
- Builds, static inspection and audited fixture tests may continue without launching the retail game. Keep fixture evidence separate from real gameplay evidence.
- Runtime tests require a separately qualified background worker: a never-displayed Windows desktop with an in-process scenario bridge, or an isolated Windows VM/remote machine. Stop if the worker cannot run invisibly; never fall back to a visible launch. Worker implementation is now authorized; runtime admission still requires actual qualification.
- Agents may inspect and implement separate assigned components in parallel. One coordinator owns integration, builds and any runtime-worker session; agents do not independently install loaders or launch games. Use separate build directories and immutable per-run artifacts once supported.
- Preserve retail EXE/Steam DLL hashes, copied-save/offline gates, owned installation rollback and the existing exact-native-call review rules in every worker environment.
- The user subsequently requested implementation of the worker and continuation of mod development. Work may resume under these background-only constraints; a visible runtime session remains unauthorized. See tools/lab/README.md for deployment and admission checks.

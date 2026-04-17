# Refactor: File-Size Triage (/reduce-file)

Source: /external-refactor-clean on Projects/BrokenEngineSandbox/Source/Network (recursive)

Two files exceed the project's 500-line header / 1000-line implementation soft thresholds meaningfully enough to route to `/reduce-file`. Two others sit near the threshold but are expected to shrink once `Refactor_SendBoilerplate.md` and `Refactor_ClientReceiveStateMachine.md` land.

## Changes

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp (808 lines)
- Run `/reduce-file C:/Users/dougt/Documents/BrokenEnginePublic/Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp`. [~15m setup + /reduce-file-produced plan]
- Expected split axes from Phase-2 scan: pending-request processors, lifecycle hooks, save/load.

### Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.cpp (668 lines)
- Run `/reduce-file C:/Users/dougt/Documents/BrokenEnginePublic/Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.cpp`. [~15m setup + /reduce-file-produced plan]
- Note: the 207-line `ReconcileCoord` function decomposition is already in `Refactor_OversizedFunctions.md` and should land first; the remaining file may no longer need splitting after that.

### Deferred (re-measure after other refactors)
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp` (537 lines) — expected ~390 after `Refactor_SendBoilerplate.md`. Do not split.
- `Engine/Source/Network/Client/ClientReceive.cpp` (501 lines) — expected ~425 after `Refactor_ClientReceiveStateMachine.md`. Do not split.
- `Engine/Source/Network/Server/ServerReceive.cpp` (552 lines) — expected ~500 after `Refactor_GuardConsolidation.md` removes the 11 duplicate gates. Do not split.

## Verification
- After `/reduce-file` lands for each target: rebuild both configs; confirm no behavior change.
- Re-measure the four deferred files after the upstream refactors; re-triage if any still exceeds 500/1000.

## Verification Notes
Verified line counts via `wc -l` at commit d08678d3:
- `ServerFleetManager.cpp` — 808 lines (matches plan)
- `ReconcileReplay.cpp` — 668 lines (matches plan)
- `ClientSession.cpp` — 537 lines (matches plan)
- `ClientReceive.cpp` — 501 lines (matches plan)
- `ServerReceive.cpp` — 552 lines (matches plan)
Deferral rationale is consistent with the downstream refactor plans (`SendBoilerplate`, `ClientReceiveStateMachine`, `GuardConsolidation`).

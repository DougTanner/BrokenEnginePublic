# Restore Release Static-Analysis Build Cleanliness

## Context

The client/server Release builds currently fail because `/analyze` warnings are promoted to errors in four unchanged baseline regions. Debug and Profile builds pass, and the active DataPacker worktree-output change does not edit these sources, so the user explicitly removed Release from that change's acceptance rather than expanding its scope.

Verified warning sites in current source:

- `ImGuiManager::Prepare` dereferences `gpAgentInput` in the `PhysicalInputSuppressed()` branch at `Engine/Source/Graphics/Managers/ImGuiManager.cpp:490`; the pointer is checked in the preceding independent branch, but PREfast reports C6011 at the later dereference.
- `PackChunks::DecommitChunkRange` intentionally calls `VirtualFree(..., MEM_DECOMMIT)` at `Engine/Source/File/PackChunks.cpp:834`; PREfast reports C6250 because the call deliberately does not use `MEM_RELEASE`. The existing file contract requires decommitting only page-aligned interiors while retaining the reserved lazy-pool address range.
- The two engine/game contiguous-index `static_assert`s at `Engine/Source/Profile/ProfileManagerBase.cpp:14-15` compare distinct enum types; Release reports C5054 even though the comparisons are compile-time index-contract checks.
- `CollectLogLines` in `Projects/BrokenEngineSandbox/Source/Agent/AgentCommands.cpp:66-83` fills fixed-capacity `pLines` and `pMatching` arrays from `LogBuffer::Tail`; Release reports C6385/C6001 because analysis does not prove `iFilled`/`iMatchCount` remain within `BUFFER::kiLineCount`. `LogBuffer::Tail` currently bounds its return through `min(iAvailable, iMaxLines)` and callers pass at most `kiLineCount` (`Common/Log/Log.h:44-73`).

These are one cohesive build-hygiene batch: each blocks the same client/server Release verification command, each is a narrow static-analysis contract repair, and all can be accepted by rebuilding both Release targets with code analysis enabled.

## Design

1. Make the `ImGuiManager::Prepare` pointer precondition visible at the dereference without adding a useless crash-adjacent assertion. Preserve the established behavior that physical-input suppression is active only for an agent-port client; a missing agent input must fail closed or skip the synthetic-pin query rather than dereference null.
2. Preserve `PackChunks::DecommitChunkRange`'s `MEM_DECOMMIT` semantics and reserved address range. Resolve C6250 with the narrowest site-specific analyzer annotation/suppression and a comment explaining why `MEM_RELEASE` would violate the recommit contract; do not change the call to release the pool.
3. Express the two profile enum-position comparisons through a common integer/underlying representation so the compile-time equality contracts remain enforced without cross-enum C5054.
4. Make `CollectLogLines`' fixed-array bounds mechanically visible to PREfast. Preserve chronological filtering and count behavior; prefer a checked bound derived from `BUFFER::kiLineCount` or a justified analysis postcondition over heap allocation or a blanket warning disable. If the durable contract belongs on `LogBuffer::Tail`, keep runtime behavior unchanged and cover all callers rather than adding an Agent-only fiction.
5. Rebuild client and server Release with code analysis enabled and warnings-as-errors. Confirm the four cited diagnostics are absent and no warning suppression is broader than its proven site.

## Critical files

- `Engine/Source/Graphics/Managers/ImGuiManager.cpp` — `ImGuiManager::Prepare` physical-input suppression branch.
- `Engine/Source/File/PackChunks.cpp` — `PackChunks::DecommitChunkRange` intentional `MEM_DECOMMIT` call.
- `Engine/Source/Profile/ProfileManagerBase.cpp` — engine/game enum-position `static_assert`s.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommands.cpp` — `CollectLogLines` fixed-capacity filtering arrays.
- `Common/Log/Log.h` — `LogBuffer::Tail` capacity/return contract, only if the analyzer proof is correctly owned by the shared helper.

## Out of scope

- Changing lazy-pool reservation, decommit, recommit, or chunk-loading behavior.
- Refactoring profile virtual routing, the profile enum layout, or timer/counter storage.
- Changing agent command JSON, log ordering/filtering, or ring-buffer concurrency semantics.
- Refactoring ImGui agent-input ownership or startup order.
- Cleaning unrelated Release warnings discovered after these four diagnostics are resolved; route independently with exact build evidence.

## Acceptance criteria

- Client and server Release builds pass with code analysis enabled and warnings treated as errors.
- C6011 at the agent-input dereference, C6250 at intentional lazy-pool decommit, C5054 at the enum contracts, and C6385/C6001 in `CollectLogLines` are absent.
- `DecommitChunkRange` still uses `MEM_DECOMMIT`, retains the virtual reservation, and remains compatible with `RecommitAndReloadChunkRange`.
- Agent `get_logs` output preserves its existing chronological tail, regex filtering, and count limit.
- No unit tests are added; verification is compile/static-analysis based.

## Notes

- No determinism/CRC, replay, wire protocol, `.pack`/`kiVersion`, client/server guard-scope, shader, or allocation-tracked-path behavior change is intended.
- `ImGuiManager.cpp` is client-only; `PackChunks.cpp`, `ProfileManagerBase.cpp`, `AgentCommands.cpp`, and `Log.h` participate in both targets as currently configured.
- This plan overlaps live plans in `PackChunks.cpp`, `ProfileManagerBase.cpp`, and `ImGuiManager.cpp`; changes are expected to be line-local, but the later lander must refresh citations and rerun both Release analysis builds.

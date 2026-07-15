# Replay Generation Commit Atomicity

## Context

Replay persistence uses fixed filenames for the grid, metadata, manifest, and each coordinate writer sibling set. `GameSaveLoad::SyncReplayTick` writes the new grid at recording start, then writes the valid manifest before coordinate components and metadata at stop (`Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:304-365`). If manifest publication fails, `WriteFileAtomically` preserves the previous valid manifest while later component writes can still commit. The loader then accepts that stale manifest and combines it with newer fixed-name components (`GameSaveLoad::SaveLoadReplay`, `:197-267`).

This is step-9 verification R001 and both step-10 code-audit R001 residuals. Current `SyncReplayTick`'s non-throwing stop path folds returned coordinate-writer and metadata outcomes without boolean short-circuiting, clears writer state, and emits one aggregate result, but it has no set-level generation state. `Save/RecordedCoordReplayStopPolicy.md` separately repairs the unchecked active-frame lookup that can escape before those operations and must land first. This follow-up preserves that per-component persistence contract while refining the manifest role so a *valid* manifest is published only as the final commit. A prior valid replay need not survive a failed replacement; no mixed generation may remain loadable.

## Design

Use the replay manifest as the authoritative set commit marker.

- Before the first new-generation component write at recording start, atomically replace the previous valid manifest with a state the loader rejects. If invalidation cannot be confirmed, abort before overwriting the grid or creating writers.
- Keep the marker invalid throughout recording and after every start/stop persistence failure.
- After the recorded-coordinate prerequisite establishes a non-throwing end-state policy, attempt every coordinate writer and metadata write at stop without boolean short-circuiting. Publish the valid manifest atomically only after every required data component succeeds. Manifest publication failure leaves the invalid marker in force and reports aggregate failure.
- Validate the commit marker before the load path treats metadata, grid, or coordinate files as one generation. Keep the final valid manifest's current serialized shape unless an approved version decision says otherwise.

Keep generation policy local to F7 replay persistence; do not add a general filesystem transaction abstraction.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp` — `SyncReplayTick` start/stop ordering and `SaveLoadReplay` commit validation.
- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.h` — replay persistence helpers/state if needed.
- `Engine/Source/File/FileManager.h` / `.cpp` — existing atomic-write contract; extend only if replay invalidation cannot be confirmed with the existing API.
- `Projects/BrokenEngineSandbox/Source/AGENTS.md` and `Engine/Source/File/AGENTS.md` — durable replay-set/atomic-file contracts if ownership text changes.

## Out of scope

- Preserving the previous replay after a failed replacement.
- Multiple replay slots, naming/UI changes, or background persistence.
- Broad transaction APIs or unrelated appdata cleanup.
- Simulation, CRC-input, network, or save-grid serialization changes.

## Acceptance criteria

- No fixed-name replay component is overwritten until the previous valid manifest is confirmed non-loadable; invalidation failure aborts start and creates no writers.
- During recording and after any required component failure, F8 load rejects the replay before constructing readers from mixed files.
- With `Save/RecordedCoordReplayStopPolicy.md` landed, stop attempts every coordinate writer and metadata write, publishes a valid manifest only after total data success, clears writers, and emits the aggregate result at the correct severity.
- Starting from a good replay, deterministic obstructions of grid, one coordinate sibling, metadata, invalidation, and final manifest publication never leave a loadable mixed generation.
- An unobstructed record/stop remains loadable with current valid bytes unless a separately approved manifest-version change is required.

## Notes

Prerequisite: `Save/RecordedCoordReplayStopPolicy.md` lands first and owns recorded-coordinate end-frame lifetime; this plan retains independent set-commit scope. Server/debug replay only. Persisted frames contain CRC-checked state, but this plan must not change sim order/content, `game::Frame::kiVersion`, `.pack`, guards, or wire protocol. If valid manifest format changes, route version/compatibility through the grill; add no backward compatibility without consent. Build both targets and use `/agent-harness` for prior-good replay, injected failures/load attempts, and unobstructed playback.

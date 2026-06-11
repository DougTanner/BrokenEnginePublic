# Architecture: LogDifferences Walks ServerCollections

## Context

Source: /external-architecture-review on `Engine/Source/Frame` (non-recursive). `FrameInterpolateBase::Crcs()`/`FramePostRenderBase::Crcs()` walk the `ServerCollections()` tuple via fold (`FrameBase.cpp:14-17, 79-82`), but the sibling `LogDifferences` implementations hardcode the shared collection list (`explosions`/`pushers` named literally at `FrameBase.cpp:29-30, 100-101`). Adding a third shared collection updates the CRC automatically but silently omits it from desync diagnosis — exactly the situation where `LogDifferences` is needed most. `Frame/CLAUDE.md` already documents the intended contract ("`ServerCollections()` is the cross-build-shared subset walked by CRC, `ServerRead`, and `LogDifferences`"), so this is also a code-vs-doc mismatch.

## Design

### Engine/Source/Frame/FrameBase.cpp
- Rewrite `FrameInterpolateBase::LogDifferences` (lines 29–30 region) and `FramePostRenderBase::LogDifferences` (lines 100–101 region) to fold over the same `ServerCollections()` tuple that `Crcs()` walks (lines 14–17 / 79–82), calling each collection's `LogDifferences` member, instead of naming `explosions`/`pushers` literally [~15m]

## Critical files
- `Engine/Source/Frame/FrameBase.cpp`

## Acceptance criteria
- Adding a hypothetical new shared collection to `ServerCollections()` would require zero `LogDifferences` edits.
- Desync log output for the existing two shared collections is unchanged (same collections compared, same per-collection reporting).

## Out of scope
- Changing what each collection's `LogDifferences` member reports
- Client-only collections (stay excluded from determinism diagnosis by design)
- Any CRC computation change (`Crcs()` untouched)

## Notes
- Diagnostic-path-only change — `LogDifferences` runs when a desync is already detected; no sim behavior, no CRC value, no serialization change.

## Verification Notes (2026-06-10)
- All cites exact: `Crcs()` folds `ServerCollections()` via `std::apply` at `FrameBase.cpp:14-17` and `:79-82`; `LogDifferences` names `explosions`/`pushers` literally at `:29-30` and `:100-101`. `ServerCollections()` is `std::tie(explosions, pushers)` for both bases (`FrameBase.h:117-120, 224-227`).
- Fold implementability confirmed: both tuple members already expose `LogDifferences` (the very calls being replaced); the fold needs paired iteration over `this->ServerCollections()` and `rOther.ServerCollections()` (index_sequence or two-tuple `std::apply`), same shape as `AllocateAndCopyCollections` (`FrameUtils.h:139-144`).
- `Frame/CLAUDE.md` doc-contract claim confirmed ("`ServerCollections()` is the cross-build-shared subset walked by CRC, `ServerRead`, and `LogDifferences`") — the code currently violates it for `LogDifferences` only.
- Related (out of this plan's engine-only scope, noting for completeness): the game layer has the same divergence shape — `game::FrameInterpolate::Crcs` folds `GameInterpolateCollections` (`Frame.cpp:543-546`) while `FrameInterpolate::LogDifferences` hardcodes `pBlasters`/`pMissiles`/`pSpaceships`/`pTargets` (`:560-563`); no collection is currently omitted there, but the same fold treatment would future-proof it.

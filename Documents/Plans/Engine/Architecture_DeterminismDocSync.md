# Architecture: Determinism Doc & Tuple-Order Contract Sync

## Context
Source: /external-architecture-review on Engine/Source (recursive). The simulation/threading review found the engine's determinism machinery clean, but the canonical contract document describes a CRC system that no longer exists, and one load-bearing ordering contract is enforced only by accident of tuple position.

## Design

### Documents/FloatingPointDeterminism.txt
- Rewrite §10 (:129-139): it claims `Crc()` "includes all collections (client self-check)" plus a separate `ServerCrc()`, but the code has one shared CRC — both `FrameInterpolateBase::Crcs()` and `FramePostRenderBase::Crcs()` walk `ServerCollections()` only (`FrameBase.cpp:8-22, 71-86`; `Frame/CLAUDE.md:28` documents the single-shared-CRC model). No client-inclusive CRC or `ServerCrc()` symbol exists. Doc-only [~15m]

### Engine/Source/Frame/FrameBase.h
- Add a comment at the `Collections()` tuples (:94-109): owner collections that Sync into owned collections must precede them in tuple order — `ExplosionsUpdate.cpp:93,109` writes current-frame `smokeTrails.pVecPositions` that `SmokeTrails.cpp:49-65` smooths in the same walk; correct only because explosions (index 2) precede smokeTrails (index 8). Reordering the tuple looks free (CRC/count locks don't care) but introduces a one-frame trail lag [~5m]

## Critical files
- `Documents/FloatingPointDeterminism.txt`
- `Engine/Source/Frame/FrameBase.h`

## Out of scope
- Enforcing the tuple order structurally (a static_assert on type positions is possible but YAGNI for a 2-collection dependency; the comment names the rule)
- The visual-error decay fix (moved to `Engine/Refactor_RootFilesQuickWins.md` — it's a behavior edit, not doc)

## Notes
- Invariant exposure: none — documentation and comment only
- No open decisions

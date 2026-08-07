<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T21:59:57.368Z","dependsOn":[]} -->
# Reduce oversized header Collection.h

## Context

`Engine/Source/Frame/Collections/Collection.h` measures 6,372 `bt-token-v1`, over the 5,000 header threshold, and was flagged `reduceFileCandidate` by the analysis manifest during the 2026-08-06 `/external-deep-analysis` run over `Engine/Source/Frame/Collections` (Recursive). This is a routing observation from that pipeline's file-size triage, not a proven defect; the pipeline requires the oversized file to be passed through with its instruction intact.

## Design

run /reduce-file Engine/Source/Frame/Collections/Collection.h

`/reduce-file` owns the analysis and the decision-complete reduction plan; execute this Plan by running it and implementing the reduction it proves. Collection SOA storage layout is CRC-load-bearing and must not gain an indirection (`Engine/Source/AGENTS.md`, aggregation rule exemption).

## Critical files

- `Engine/Source/Frame/Collections/Collection.h` — the reduction target.
- `Engine/Source/Frame/Collections/AGENTS.md` — read-only collection-contract authority.

## In scope

- `run /reduce-file Engine/Source/Frame/Collections/Collection.h` and the behavior-preserving reduction it proves, confined to `Collection.h` and any split-out sibling headers plus their include/project membership updates.

## Out of scope

- Any change to member layout, allocation, persistence, serialization, CRC participation, or identity semantics.
- Any behavioral change to collection mechanics; other framework headers unless `/reduce-file` proves a move.

## Risk tier and invariants

Expected Change Workflow Tier 2: behavior-preserving restructuring of a widely included CRC-load-bearing framework header; escalate to Tier 3 if the reduction touches serialization or layout surfaces. Invariants: PostRender state stays bit-identical, per-tick CRC unchanged, no SOA storage indirection introduced.

## Acceptance criteria

- `Collection.h` measures at or below 5,000 `bt-token-v1` after the reduction, or the `/reduce-file` analysis records a named irreducibility residual.
- Client and server build clean through `/compile`.

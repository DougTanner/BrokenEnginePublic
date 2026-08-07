<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T13:08:49.594Z","dependsOn":[]} -->
# Remove dead Camera member mVecCameraOffsetSmoothed

## Context

`Projects/BrokenEngineSandbox/Source/Graphics/Camera.h:53` declares `XMVECTOR mVecCameraOffsetSmoothed {};` with zero readers or writers repository-wide (exact `git grep` matches only the declaration; verified by /external-deep-analysis Phase-3 review, 2026-08-07, including absence of `sizeof(Camera)`/`offsetof(Camera, ...)` dependencies). Camera's class layout is not serialized: persisted camera state goes through the versioned client-settings POD (`Projects/BrokenEngineSandbox/Source/AGENTS.md`, "Persisted client settings are versioned POD"), which a member with no readers cannot participate in.

## Design

Delete the single declaration line. No replacement, no other edits.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Graphics/Camera.h` — the `mVecCameraOffsetSmoothed` declaration (currently :53)

## In scope

- Removing that one declaration.

## Out of scope

- Any other `Camera` member, comment, or layout change.

## Risk tier and invariants

Tier 1 — dead-code removal with no consumers; client-only class whose raw layout is neither serialized nor CRC-participating. The class size change is compile-checked only.

## Acceptance criteria

- Repository-wide search for `mVecCameraOffsetSmoothed` returns nothing; client compiles.

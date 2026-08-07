<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T22:03:18.001Z","dependsOn":[]} -->
# Decompose FileManager worktree initialization and materialization

## Context

Verified by `/external-deep-analysis` over `DataPacker/Source` (baseline `3cb5e9a6`), with seams confirmed against source by a fresh reviewer:

- `FileManager::InitializeWorktreeOutputs` (`DataPacker/Source/FileManager.cpp:269-398`, cyclomatic complexity 33) interleaves destination classification (`:271-287`), Git identity discovery ending fail-closed (`:299-348`), source configuration (`:349-361`), and per-root reconciliation (`:362-396`), obscuring which failures must occur before noninteractive diagnostic mode is enabled.
- `FileManager::MaterializeOutput` (`FileManager.cpp:420-547`, cyclomatic complexity 26) chains source/state validation (`:422-436`), deterministic inventory plus allocation (`:437-470`), capacity decision (`:471-494`), staging acquisition (`:495-509`), copy/publication with rollback (`:510-543`), and state publication (`:544-546`), making the point after which the primary link is removed hard to audit.

Both functions implement the linked-worktree identity, fail-closed reparse, and copy-on-write contracts in `DataPacker/Source/AGENTS.md`.

## Design

Extract file-local, behavior-preserving helpers at the verified whole-phase seams only:

- `InitializeWorktreeOutputs`: an identity-discovery helper (returning only data the function already produces) and a per-root reconciliation helper. Validation order, early returns, state transitions, source-path assignment, materialization fallback, and the placement of `diagnostic::MarkValidatedLinkedWorktree()` at `:349` are preserved exactly.
- `MaterializeOutput`: inventory/allocation and staging-acquisition helpers. The mutation sequence — recognized-link removal (`:520-523`), rename (`:524`), restoration/cleanup (`:526-542`) — stays under one rollback owner; a publication helper is acceptable only if it owns that complete transaction.

Acceptance target from the analysis pipeline: no resulting function exceeds cyclomatic complexity ten. Verifier caveat, carried verbatim as a binding constraint: that number is not a repository authority — extract whole-phase seams only, never cosmetic micro-helpers to chase it; if the target and the seams conflict, record the residual and surface the conflict instead of splitting cosmetically.

## Critical files

- `DataPacker/Source/FileManager.cpp`
- `DataPacker/Source/FileManager.h` (only if a file-local helper needs a declaration; no public signature changes)

## In scope

- Behavior-preserving extraction of the phases listed above inside `InitializeWorktreeOutputs` and `MaterializeOutput`, plus the file-local helper declarations those extractions require.

## Out of scope

- Any behavior, ordering, diagnostic, or filesystem-effect change; CLI parsing; ThirdParty resolution; other `FileManager` members.
- Splitting the copy/publish/rollback transaction across owners.

## Risk tier and invariants

Expected Change Workflow Tier 3. Trigger: this code is the worktree output copy-on-write used by `/compile` build coordination; a regression can block other sessions. Invariants: identical state and filesystem effects for linked, primary, Git-unavailable, malformed-metadata, absent-source, recognized-link, and unsupported-link scenarios; monotonic diagnostic-mode transition preserved; low-disk cancellation preserves the symlink.

## Acceptance criteria

- Behavior-preserving refactor verified by scenario coverage over the cases named in the invariants line, each with unchanged state and filesystem results.
- Each listed function is decomposed to the stated target or carries a named residual explaining the remaining seam conflict.

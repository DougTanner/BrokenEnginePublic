# C++ change process regression reference

This reference dry-runs the seven named C++ Code Change Process stages against three completed sessions. It is a process regression oracle, not authoritative evidence for a new change. The normalized reports under [`reporting-fixtures/`](reporting-fixtures/README.md) are frozen test inputs; a live workflow must still produce file-backed reports for its own fixed baseline and current bytes.

## Frozen corpus identities

| Session | Landed commit | Reports | Bytes | Source-manifest SHA-256 |
|---|---|---:|---:|---|
| DataPacker diagnostics | `d730205ab92e91b48f46d29076f12165dde8dda5` | 21 | 112270 | `4655da5ac8ffc7db53eed3e8e8f58872f7bb9047f7677a103ecab3dde4b0fa33` |
| PREfast cleanup | `98b5272c0cb836b822cc5692484107768426b103` | 51 | 328170 | `543e943a887335cafaaf75dfc573d04f544483ad49f7b254c7aa1a69a9508597` |
| Save failure reporting | `90f7ed3450b9318d89b3de74cb7e1271f12789ce` | 48 | 362650 | `7b462e9a720441dff0316c7c6fe1d8b6d70233df29c8d7e074b61aefc09f3120` |

## Evidence-stop matrix contract

Every execution-control record and final verification ledger must contain one row per approved acceptance criterion and declared invariant. Each row names: the criterion or invariant; risk trigger; decisive check; expected observable result; current evidence report/ID; status (`PASS`, `FAIL`, `BLOCKED`, or `UNVERIFIED`); and the exact earlier evidence invalidated by any later byte change. Every in-scope row must be `PASS`; never use `SKIP` or `SKIPPED` to dispose of one. Conditional roles or checks whose objective trigger does not apply may be recorded separately as `N/A` outside the acceptance-row set. One check may satisfy multiple rows. A duplicated check must name its distinct independent signal; reviewer count is not an independent signal by itself.

The workflow stops when every row is `PASS`, the final-tree content manifest covers the current bytes, no unresolved accepted finding remains, and reconciliation has invalidated no cited evidence. A failed row loops back only to its affected implementation region, review hypothesis, and check. New exploratory scenarios require an approved plan delta; they cannot be added merely because the decisive matrix already passed.

## DataPacker diagnostics dry run — Tier 2

Tier trigger: one offline DataPacker subsystem changes diagnostic behavior. It does not expose simulation/CRC, wire, save/replay format, threading, client/server guard scope, or shared build/bootstrap coordination.

Required route:

- **Approve and classify:** record Tier 2 and the accepted interactive versus validated-linked-worktree behavior, exactly-once diagnostic ownership, unchanged fatal/cancel outcomes, and DataPacker Release build/project membership criteria.
- **Implement and propagate:** use `/implement-plan`; invoke `/update-affected-code` because the new reporter changes diagnostic ownership semantics and emits a sweep handoff. Preserve the material caller propagation that found and updated the missed attribution path.
- **Run targeted pre-review checks:** compile DataPacker Release after project membership is present.
- **Review and resolve correctness:** run one `/repo-code-review`. No default adversarial partner is required because the change is Tier 2 and no unresolved concrete failure hypothesis remains.
- **Apply conditional hygiene:** run C++ style, durable DataPacker documentation, and vcxproj/filter membership checks because all three triggers are present.
- **Verify the acceptance matrix:** prove complete structured records for top-level, export aggregate, insufficient-disk, and low-disk paths; exactly-once ownership; modal retention for ordinary/unknown identity; modal suppression plus forced Cancel in validated linked worktrees before output mutation; unchanged fatal exit; exact new source/filter membership; and a passing DataPacker Release build.
- **Reconcile, audit when triggered, and finalize:** after byte-preserving reconciliation, no whole-change audit is required. If reconciliation or a late semantic fix changes bytes, run one whole-change audit over the complete DataPacker change.

Exact evidence stop: stop when the listed diagnostic scenarios, membership check, and Release build all pass on the final bytes and the propagation handoff is closed. Do not run paired correctness/adversarial reviews or paired audits for code, docs, and queue groups.

## PREfast cleanup dry run — Tier 3

Tier trigger: Release static-analysis and compile-policy changes affect both client and server and include build/session coordination that can block other sessions.

Required route:

- **Approve and classify:** record Tier 3, the four warning families, the preserved `MEM_DECOMMIT` reservation invariant, unchanged log-tail behavior, client/server Release analysis targets, and coordination/queue criteria.
- **Implement and propagate:** use `/implement-plan`; run `/update-affected-code` only for emitted handoffs or changed contracts such as the shared `LogBuffer::Tail` bound.
- **Run targeted pre-review checks:** build client and server Release with code analysis and warnings-as-errors before review.
- **Review and resolve correctness:** run one `/repo-code-review` and one bounded `/adversarial-review` because Tier 3 applies. Adjudicate their union once; do not rerun for consensus.
- **Apply conditional hygiene:** run C++ style; update durable build instructions only where the policy changed; skip vcxproj membership work unless file membership or affinity changed.
- **Verify the acceptance matrix:** prove both Release analysis builds pass; C6011, C6250, C5054, C6385, and C6001 are absent at the approved sites; `DecommitChunkRange` still uses `MEM_DECOMMIT` and retains recommit compatibility; `get_logs` retains chronological tail, regex filtering, and count limits by focused static/code evidence; and required queue/build-policy coordination state is coherent.
- **Reconcile, audit when triggered, and finalize:** run one whole-change audit only if reconciliation changes bytes, late semantic fixes occur, integration remains unseen, or a changed region lacked correctness review.

Exact evidence stop: the passing client/server Release PREfast logs plus focused invariant checks are decisive. Do not launch the game or agent harness; runtime client/server smoke tests are unrelated to this build-policy contract.

## Save failure reporting dry run — Tier 3

Tier trigger: the change spans save, file, agent-command, and server-network owners and changes return semantics at their integration seams. The approved scope does not change wire or save formats.

Required route:

- **Approve and classify:** record Tier 3; atomic-write result propagation; command error-versus-success envelopes; client-request warning behavior; unchanged void-context callers; approved filename scenarios; no wire/save-format delta; and affected client/server compilation.
- **Implement and propagate:** use `/implement-plan`; require `/update-affected-code` for the `WriteGrid`/`ServerSave` signature and semantics changes and their callers.
- **Run targeted pre-review checks:** compile affected server code and any client target sharing changed headers before review.
- **Review and resolve correctness:** run one `/repo-code-review` and one bounded `/adversarial-review` for the Tier-3 cross-owner integration, then adjudicate their union once and focus fixes/retests on proven failures.
- **Apply conditional hygiene:** run C++ style and durable docs because both changed; skip vcxproj when no file or affinity change occurred.
- **Verify the acceptance matrix:** prove a normal agent save returns the requested file only after a successful atomic write; one approved failing-write scenario returns an error envelope; the approved reserved-name cases are rejected before writing when that rider is included; the fire-and-forget client request logs failure without adding a wire acknowledgement; existing discard-return callers compile; affected client/server builds pass; and save/wire formats remain unchanged by inspection.
- **Reconcile, audit when triggered, and finalize:** because this is Tier-3 cross-file integration, run one whole-change audit after reconciliation, then rerun only checks invalidated by accepted fixes.

Exact evidence stop: stop after the approved successful save, failing-write, reserved-name (when included), client-request logging, compile, and format-invariant rows pass on final bytes. Do not invent additional obstruction or filesystem variants after those approved scenarios pass, and do not split the final audit into paired file-group or queue-prose audits.

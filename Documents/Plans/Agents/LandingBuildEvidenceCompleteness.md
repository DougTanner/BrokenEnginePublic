<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-02T01:12:00.962Z","dependsOn":[]} -->
# Landing Build Evidence Completeness

## Context

Two related gaps make typed build evidence incomplete at the landing gate, each proven to cost a full extra verification round on unchanged bytes:

1. The `builder` role's return contract is "build status plus decisive error and warning lines verbatim" (`.claude/agents/builder.md:3`, mirrored in the root `AGENTS.md` role table), not the typed `broken-engine-build-result` envelope `/verify-changes` requires as evidence. In landing `000311fd` (Claude session `050f3482`), `/verify-changes` round 1 marked the build evidence UNVERIFIED because the handoff summarized the envelope; recovery was a re-dispatch that re-emitted both envelopes plus a full second verification round — ≈11.4 minutes of active agent-time across three agents with zero byte changes in between.
2. The DataPacker build produced a log but no typed receipt during landing `a7280bc9` (Claude session `2180aa65`, 21:28:58Z): `/verify-changes` blocked solely on the missing receipt, and recovery was a `builder` rebuild (21:31:24-21:32:49Z) plus a complete second verification pass over a byte-identical diff.

Root cause: the producer contracts (builder handoff, per-target receipt emission) are weaker than the consumer contract (`/verify-changes` typed-envelope evidence), so the gap is discovered at the most expensive point — the landing gate.

## Design

1. Change the `builder` return contract to include the emitted `broken-engine-build-result` envelope verbatim in every handoff, in `.claude/agents/builder.md`, the root `AGENTS.md` role-table Work cell, and the `/compile` handoff wording, stated once at the owning site and referenced elsewhere. Decisive error/warning lines remain alongside it for failures.
2. Reproduce the DataPacker receipt gap against the current `/compile` driver path: build DataPacker through the documented `/compile` flow and compare its emitted artifacts with a client/server target's. If the receipt is genuinely absent, bring DataPacker to parity in the compile skill's driver scripts; if the current flow already emits it, land only the contract change from item 1 and record the premise drift in the completion notes.

## Critical files

- `.claude/agents/builder.md` — return contract.
- `AGENTS.md` — the `builder` row's Work cell.
- `.agents/skills/compile/SKILL.md` and its scripts — handoff wording; DataPacker receipt emission parity if the premise check confirms the gap.

## In scope

- `builder.md`, root `AGENTS.md` builder row, and `compile` handoff wording: the verbatim-envelope requirement.
- The DataPacker receipt parity fix in the compile skill scripts, only if the premise check reproduces the missing receipt.

## Out of scope

- WorktreeCli's serialized MSBuild driver internals and AgentTools bootstrap/promotion policy; if the receipt gap proves to live inside WorktreeCli itself rather than the skill scripts, stop and report the Tier-3 escalation instead of editing it under this plan.
- The `broken-engine-build-result` schema.
- `/verify-changes` behavior (owned by `Documents/Plans/Agents/VerifyChangesRetryableEvidence.md`).

## Risk tier and invariants

Tier 2 — role-contract documentation plus scoped compile-skill script behavior; escalates per the out-of-scope rule if the shared build driver itself needs the edit.

Invariants: build outcomes and targets are unchanged — only evidence emission and handoff completeness change; a failed build still surfaces its decisive lines.

## Acceptance criteria

- The builder contract at all three documentation sites requires the verbatim typed envelope.
- After the change, a `/verify-changes` pass consuming a builder handoff finds the typed envelope without a re-dispatch; if the DataPacker gap was confirmed, a DataPacker build emits the same receipt shape as the client/server targets.
- `/validate-skill` passes on the edited `compile/SKILL.md`.

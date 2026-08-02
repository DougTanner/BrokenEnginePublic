<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-02T01:12:09.870Z","dependsOn":[]} -->
# Delegation Handoff Termination Contract

## Context

`.agents/references/subagent-reporting.md` defines the handoff form and interruption ladder but no termination rule for the worker side, and several proven costs trace to that gap:

- A worker delivered its result then blocked in an open-ended wait: Codex locator `019fbeb6-9bc3` (landing `c7a85890`) sent its handoff at 19:07:40Z, then issued two `wait` calls with `timeout_ms: 3600000` and idled 71 minutes (~2.0 M cumulative input tokens) until the parent had to `interrupt_agent` (parent `019fbeb5`, 20:15:34Z). The finalizer in landing `3ebf662f` (`019fbeec-da7c`) likewise sent its landing summary then blocked a full hour in `wait_agent` after its parent's turn had already ended.
- A healthy worker was interrupted on elapsed-time judgment: parent `019fbe73` (landing `e93f52fa`) interrupted its finalizer at 19:05:50Z citing "has not returned after the scripted phase's expected window" while the child had made tool calls 1 s and 9 s earlier — against the reference's own rule that elapsed time alone is not no-progress evidence — costing a recovery-audit turn.
- ~20 of 53 inter-agent messages in landing `3ebf662f` were no-decision progress pings, each waking a parent narration turn.
- A manager-transcribed session baseline needed a mid-task correction round-trip: parent `019fbeb5` re-sent the authoritative 40-character baseline at 20:11:17Z after the worker flagged a transcription ambiguity.

Root cause: the delegation reference specifies what a handoff contains but not how a worker's turn ends, when it may message mid-task, how the manager proves no-progress, or where brief identity values come from.

## Design

Edits to `.agents/references/subagent-reporting.md`, each stated once:

1. Termination: a worker ends its turn with the handoff as its final answer and never enters an open-ended wait after delivering it; continuation uses the host's resume path (`followup_task` / `SendMessage`), which the reference already prefers. Any wait a worker does issue mid-task carries a bounded timeout well under the host tool cap.
2. Mid-task messages: worker→manager messages are for blocking questions and partial handoffs only, never status narration. Manager→user narration is unaffected.
3. Interruption precondition: replace the judgment-form trigger with a mechanical one — documented no-progress means no new tool call or message within a fixed window measured from the worker's last recorded action; elapsed turn time alone remains insufficient, as already stated.
4. Brief identity: the session baseline and other machine-derivable identity fields in a task brief are copied from script output (the session-context or inventory script that already emits them), never retyped from memory or scrollback.

Plus one `/finalize-changes` sentence: the stop-before-confirmation handoff is a terminal return; the manager resumes the same finalizer after the user confirms.

## Critical files

- `.agents/references/subagent-reporting.md` — termination, mid-task message, interruption, and brief-identity clauses.
- `.agents/skills/finalize-changes/SKILL.md` — the pre-confirmation terminal-return sentence.

## In scope

- `subagent-reporting.md`: the four clauses above.
- `finalize-changes/SKILL.md`: the one terminal-return sentence at the confirmation boundary.

## Out of scope

- Host tool behavior, timeouts, or scheduler code; these are contract sentences agents follow.
- The handoff field list and the recovery-capsule content, which stay as written.
- Role definitions and the delegation routing table.

## Risk tier and invariants

Tier 1 — documentation contract edits in one reference and one skill, with no behavior, script, or invariant exposure.

Invariants: the handoff form is unchanged; resume-over-replace preference is unchanged; the interruption ladder keeps its ordered steps, only the evidence test becomes mechanical.

## Acceptance criteria

- `/validate-skill` passes on the edited `finalize-changes/SKILL.md`.
- The reference states all four clauses exactly once each, and the interruption trigger names a measurable condition (no recorded worker action within a fixed window) rather than an expected-duration judgment.

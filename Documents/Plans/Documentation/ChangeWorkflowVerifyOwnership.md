<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-02T01:12:11.927Z","dependsOn":[]} -->
# Change Workflow Verify Ownership And Foreign Movement

## Context

Two root `AGENTS.md` Step 7/8 gaps produced improvised or silently skipped process in the audited landings:

1. `/verify-changes` dispatch ownership contradicts between documents. Root `AGENTS.md` Step 8: "main then dispatches one fresh read-only `/verify-changes` reviewer". `.agents/skills/finalize-changes/SKILL.md:21`: "The single landing `/verify-changes` pass runs inside the" finalizer, and its step 3 has the finalizer dispatch it. In landing `a7280bc9` (Claude session `2180aa65`) the session followed the skill — the finalizer dispatched both verification passes — and Step 7's separate fresh read-only acceptance-table reviewer consequently never ran: the producing `implementer` pronounced its own acceptance evidence sound (21:15:33Z), and the first independent mapping of criteria to evidence happened at Step 8. Which required control runs currently depends on which document an agent reads first.
2. Step 8 defines re-review triggers only in terms of the session diff — "a conflict resolution, changed session bytes, or changed semantics — re-runs review of the affected regions… a clean identical rebase does not" — and says nothing about foreign primary movement when session bytes are unchanged. In landing `3ebf662f` (Codex session `019fbea8-abbe`) four foreign landings advanced primary during the confirmation pause; the manager first concluded no re-review was triggered (23:58:12Z), then reversed itself when the foreign changes touched code adjacent to the session's (00:02:34Z, 00:04:08Z) and ran a full re-review, rebuild, and harness rerun (~22.5 minutes, ~7.8 M tokens) without re-asking confirmation. The judgment was defensible but had no contract basis in either direction.

## Design

Two root `AGENTS.md` edits, mirrored where the finalize skill states the same rule:

1. Ownership: Step 8 states that the finalizer dispatches the single landing `/verify-changes` pass — matching the skill and observed practice — and Step 7 states that for a stage landing in the same session, its acceptance-table verification is satisfied by that Step-8 pass; Step 7's own fresh reviewer remains required only for a stage that completes without landing. No obligation disappears: exactly one fresh read-only acceptance verification is always required, and its owner is named once.
2. Foreign movement: add one sentence to Step 8 — when primary movement after review or confirmation touches files reachable from the session's changed code while the session patch remains byte-identical, focused re-review and rebuild of the affected regions run, and the existing confirmation stands; foreign movement with no such reachability obligates nothing. This is a rule agents must apply rather than a mechanism, because the trigger is a semantic-reachability judgment no current script computes; the sentence bounds it instead of leaving it improvised.

## Critical files

- `AGENTS.md` — Change Workflow Steps 7 and 8.
- `.agents/skills/finalize-changes/SKILL.md` — the sentence stating the finalizer owns the landing verification pass, kept agreeing with Step 8.
- `.agents/skills/verify-changes/SKILL.md` — read-only; its binding contract is unchanged.

## In scope

- `AGENTS.md` Step 7: the satisfied-by-Step-8 clause for same-session landings.
- `AGENTS.md` Step 8: the finalizer-ownership statement and the foreign-movement sentence.
- `finalize-changes/SKILL.md`: only if a cross-reference sentence must change to keep the rule stated once.

## Out of scope

- `/verify-changes` internal contract, evidence rules, and diff binding.
- The confirmation contract, including when confirmation is re-asked.
- Any other Change Workflow step, the tier definitions, and the role table.

## Risk tier and invariants

Tier 1 — documentation contract corrections in the root workflow document and one skill sentence, with no behavior or invariant exposure beyond stating the existing intended control once.

Invariants: exactly one fresh read-only acceptance verification per landed stage; exactly one explicit user confirmation; a clean identical rebase still never re-asks confirmation.

## Acceptance criteria

- Root `AGENTS.md` and `finalize-changes/SKILL.md` name the same single owner for the landing `/verify-changes` pass, and Step 7 states when the Step-8 pass satisfies it.
- Step 8 contains the foreign-movement sentence with both the trigger (reachability from changed session code, byte-identical patch) and the non-trigger (no reachability) explicit.
- `/validate-skill` passes on the edited `finalize-changes/SKILL.md`.

<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-02T01:11:59.922Z","dependsOn":[]} -->
# Verify Changes Retryable And Host-Side Evidence

## Context

`/verify-changes` requires the reviewer to run WorktreeCli `plan validate` and forbids any retry within the verification (`.agents/skills/verify-changes/SKILL.md:106,114`). Under concurrent sessions, and inside the Codex `--sandbox read-only` environment, several of its required checks fail for reasons unrelated to the reviewed diff, each producing a false `BLOCKED` and a re-run:

- `plan validate` returning `code: busy` under scheduler-guard contention was the sole blocking row in both verification rounds of landing `301ad036` (Claude session `128eef4b`, `Temp/verify-out.md` and `verify-out2.md`) and recurred in landing `000311fd` (Claude session `050f3482`); in every case the manager's immediate re-run returned `status: valid, code: ok` in seconds. The audited machine ran ~10 concurrent sessions, so this contention is the normal case, not an anomaly.
- `code-quality-metrics` Compare fails inside the sandbox with a Git dubious-ownership error against the primary checkout (session `050f3482` 19:12:53Z; session `2180aa65` 21:08:38Z); the manager re-ran it host-side each time.
- One missing typed artifact forces a full second pass over a byte-identical diff: landing `a7280bc9` blocked solely on a missing DataPacker build receipt, and the recovery was a rebuild plus a complete second `/verify-changes` run (~6 minutes) with zero byte changes in between.

Root cause: the verification contract requires reviewer-side execution of checks the reviewer's environment cannot run reliably, and offers no bounded retry or single-artifact resume, so environmental noise is indistinguishable from acceptance failure.

## Design

1. Bounded busy retry: in `verify-changes/SKILL.md`, classify WorktreeCli `plan validate` `code: busy` as retryable — up to a small fixed number of re-runs within the same verification, with the final `busy` still `BLOCKED`. Deterministic tool contention is not the reviewed diff changing, so this does not weaken the no-retry rule for judgment checks, which stays as written.
2. Host-side evidence for sandbox-incompatible checks: the dispatching session runs `code-quality-metrics` Compare (and `plan validate` when the reviewer environment lacks a usable repository context) before dispatch and supplies the typed result envelopes as scope-file evidence; the reviewer validates the envelopes — schema, identity binding to the reviewed baseline/head — instead of executing the tools. State in the skill which checks are host-supplied and that the reviewer must reject an envelope not bound to the reviewed diff.
3. Single-artifact resume: when a verification's only blocking rows are missing typed artifacts and the reviewed diff hash is unchanged, a follow-up pass may re-bind only those rows, carrying every other row's verdict forward with its original citation. The resumed result must restate the full table and the diff hash it binds.

## Critical files

- `.agents/skills/verify-changes/SKILL.md` — the required-check list, the no-retry sentence, and the result-binding contract.
- `.agents/skills/codex-review/SKILL.md` — the scope-file guidance naming host-supplied evidence for verification dispatches, if a sentence is needed there.

## In scope

- `verify-changes/SKILL.md`: the three contract changes above, each stated once.
- `codex-review/SKILL.md`: at most one sentence routing host-supplied verification evidence into the scope file.

## Out of scope

- WorktreeCli scheduler-guard behavior, lock lease durations, and `plan validate` semantics.
- `code-quality-metrics` internals and the Compare baseline contract.
- The reviewed-diff binding rule: a changed diff still requires a fresh full verification.
- `/finalize-changes` and the landing confirmation contract.

## Risk tier and invariants

Tier 2 — verification-contract behavior in skill documents governing the landing gate; no engine runtime or coordination-tool code changes.

Invariants: every acceptance criterion is still mapped to evidence that settles the question on its own; host-supplied envelopes are validated against the reviewed diff's identity, never trusted bare; a resumed pass binds the identical diff hash or is invalid; a genuine acceptance failure still blocks.

## Acceptance criteria

- `/validate-skill` passes on the edited skill files.
- The skill text bounds the `busy` retry, names the host-supplied checks and their envelope-validation duty, and defines the single-artifact resume with its identical-diff precondition.

<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-31T20:51:45.582Z","dependsOn":[]} -->
# Codex Review Inputs Baseline SHA

## Context

`.agents/skills/codex-review/SKILL.md` `## Inputs` still carries the pre-script wording for its last bullet:

`- Worktree and session baseline (default to current repository root and `HEAD`)`

That default is no longer reachable. `## Method` step 1 now routes prompt assembly through `.agents/skills/codex-review/scripts/New-CodexReviewPrompt.ps1` and documents `-Baseline <full 40-character baseline SHA>`. That script forwards `-Baseline` unchanged to `.agents/scripts/Get-SessionChangeInventory.ps1` (`New-CodexReviewPrompt.ps1:218`), which rejects anything that is not a full SHA:

```
if ($Baseline -cnotmatch '^[0-9a-fA-F]{40}$') {
	Complete-SessionChangeInventory 2 'blocked' 'inventory.baseline-unresolved' "-Baseline must be a full 40-character Git SHA: '$Baseline'."
```

(`Get-SessionChangeInventory.ps1:521-522`.)

So a manager who takes the `## Inputs` default and passes `HEAD` gets a blocked exit `2`, and the skill's two sections state different contracts for the same input. The failure is visible rather than silent, which is why this is consistency debt and not a defect in the script.

The script and the `## Method` rewrite landed under `Documents/Plans/Agents/CodexReviewPromptScript.md`, whose `## Critical files` deliberately recorded `## Inputs` as unchanged. The stale bullet was therefore outside that plan's approved boundary and was reported as a residual by that session's implementer, coherence review, and acceptance verification.

## Design

Correct the one `## Inputs` bullet so it matches the contract `## Method` step 1 already states. The bullet names the worktree and the session baseline, states that the baseline is a full 40-character commit SHA, and drops the `HEAD` default. The repository-root default for the worktree is unaffected: `New-CodexReviewPrompt.ps1` still takes `-RepositoryRoot <worktree>`, and `## Method` step 1's Git Bash form derives it from `git rev-parse --show-toplevel`.

State the requirement once. `## Method` step 1 is the operative statement of the SHA form inside the invocation line, so `## Inputs` describes the input without restating the regular expression, the script name, or the blocking code.

Do not restate `-Head`, `-UntrackedPath`, `-RiskTier`, or any other script parameter in `## Inputs`; they are already documented where they are used.

Also update the stale ownership pointer in `Documents/Plans/Agents/ReviewSkillInventoryAdoption.md` `## Out of scope`, which names `Documents/Plans/Agents/CodexReviewPromptScript.md` as the owner of `.agents/skills/codex-review/SKILL.md`. That plan is completed and its file is gone, leaving a dangling reference. The exclusion itself is unchanged in effect; only the named owner becomes this plan.

## Critical files

- `.agents/skills/codex-review/SKILL.md` — `## Inputs`, the worktree-and-baseline bullet only. `## Method`, `## Manager evaluation`, `## Fallback`, and `## Notes` unchanged.
- `Documents/Plans/Agents/ReviewSkillInventoryAdoption.md` — one `## Out of scope` line; the owning-plan reference only.
- `.agents/skills/codex-review/scripts/New-CodexReviewPrompt.ps1` — read-only; the consumer of `-Baseline`.
- `.agents/scripts/Get-SessionChangeInventory.ps1` — read-only; the source of the 40-character rule.

## In scope

- `.agents/skills/codex-review/SKILL.md` `## Inputs`: the final bullet, rewritten to require a full 40-character commit SHA for the session baseline and to remove the `HEAD` default.
- `Documents/Plans/Agents/ReviewSkillInventoryAdoption.md` `## Out of scope`: the `.agents/skills/codex-review/SKILL.md` line, repointed from the completed `CodexReviewPromptScript.md` to this plan.

## Out of scope

- Any change to `.agents/skills/codex-review/scripts/New-CodexReviewPrompt.ps1`, `.agents/scripts/Get-SessionChangeInventory.ps1`, `.agents/skills/codex-review/references/prompt-template.md`, or `.agents/skills/codex-review/scripts/Test-CodexReviewPromptFixtures.ps1`.
- Relaxing the SHA requirement, adding `HEAD` resolution, or adding any baseline defaulting in either script.
- Any change to `## Method`, `## Manager evaluation`, `## Fallback`, `## Notes`, the Sol-over-reporting rule, the decide-once rule, or any review-judgment criterion.
- Any change to the frontmatter, `name`, `description`, or `allowed-tools` of `codex-review`.
- The remaining scope, invariants, and acceptance criteria of `ReviewSkillInventoryAdoption.md`; only the one owner reference changes.
- Any other skill's inputs or baseline wording.

## Risk tier and invariants

Tier 1 — mechanical documentation correction in one skill section plus one plan reference, with no public signature or invariant exposure, and no script, engine runtime, determinism/CRC, wire, serialization, save/replay, threading, or build/bootstrap coordination surface.

Invariants: the baseline contract has exactly one operative statement, in `## Method` step 1; `## Inputs` agrees with it and adds no second specification; the reviewer sandbox and manager-side split are unchanged.

## Acceptance criteria

- `.agents/skills/codex-review/SKILL.md` `## Inputs` no longer contains `HEAD` as a baseline default, and its baseline wording agrees with the `-Baseline <full 40-character baseline SHA>` form in `## Method` step 1.
- The worktree half of the bullet still permits the current repository root, so the `git rev-parse --show-toplevel` form in `## Method` step 1 remains correct.
- `Documents/Plans/Agents/ReviewSkillInventoryAdoption.md` `## Out of scope` names a plan file that exists, and still excludes `.agents/skills/codex-review/SKILL.md` from that plan.
- `/validate-skill` passes on the edited `SKILL.md`.
- WorktreeCli `plan validate` reports `status: valid` with no new diagnostics.

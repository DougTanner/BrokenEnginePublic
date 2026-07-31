<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-31T22:14:37.843Z","dependsOn":[]} -->
# Trim Review-Chain Skill Bodies

## Context

A skill body loads in full on every dispatch of that skill, and the review chain is the most frequently dispatched family in the repository: `repo-code-review`, `plan-audit`, `resolve-findings`, `glsl-review`, `codex-review`, and `adversarial-review` run on most changes, several of them more than once per change. Frontmatter descriptions (~19.5 KB, ~5,000 `bt-token-v1`) load every session and are not touched here. Root `AGENTS.md` routes to skills by name only, so no `AGENTS.md` edit is required.

Six regions in that chain either operate a script whose contract is documented elsewhere, restate root `AGENTS.md` sentence-for-sentence, or spell out default reviewer behavior:

1. `repo-code-review/SKILL.md:63-81` — `## Workflow` step 1, roughly 19 lines of `Get-CodeQualityEvidence.ps1` operating manual the reviewer reads on every C++ review: the full wrapper command line and switch list, the no-retained-file and no-inline-reconstruction rules, the digest fields to record, the `evidence.contract-mismatch` / `internal.error` / exit-`2`-forwards-`underlying` error envelope, the incomplete-review-is-`NEEDS_ACTION` rule, and the `comparison.contextChanges` instruction. `code-quality-metrics/SKILL.md:48-56` separately states the target-failure semantics (`target-parse-failure`, `target-signature-extraction-failure`, `upstream-omitted`, and the complete-parsing requirement for PASS), while that skill already owns `references/MetricContract.md`.
2. `plan-audit/SKILL.md:97-109` re-derives the execution-card field list and the risk-tier definitions from root `AGENTS.md`.
3. `resolve-findings/SKILL.md:70-72` is a verbatim copy of root `AGENTS.md` `## Diagnosis Discipline` authority order, and the Fix Workflow steps at `:45-59` narrate ordinary careful-fix procedure.
4. `glsl-review/SKILL.md:48-72` `## Correctness Checks` is generic shader-correctness material, while the skill already has a conditional reference at `references/shader-footguns.md`.
5. `codex-review/SKILL.md:87-98` `## Manager evaluation` restates root `AGENTS.md`'s decide-once and reject-speculative-findings rules sentence-for-sentence; only the interruption-fallback rule at `:92-96` is this skill's own.
6. `adversarial-review/SKILL.md:48-67` method steps and `:68-89` evidence rules describe what any competent reviewer already does.

## Design

Each region is either moved behind a citation, replaced by a pointer to the authority that owns it, or compressed. No review criterion, threshold, status vocabulary, or blocking rule changes meaning.

1. Move the operating-manual detail in `repo-code-review` `## Workflow` step 1 — the switch list, the no-retained-file and no-inline-reconstruction rules, the digest fields to record, the error-envelope codes, and the `comparison.contextChanges` instruction — into `.agents/skills/repo-code-review/references/metrics-protocol.md`. `SKILL.md` step 1 keeps the `Get-CodeQualityEvidence.ps1 -Mode Compare` entry-point invocation exactly as commit `bf4614f8` left it, plus about three lines: run Compare early; metrics are advisory and never become a finding; an operational failure leaves the review incomplete with `NEEDS_ACTION` rather than producing a finding, per that reference. Separately, `code-quality-metrics/SKILL.md:48-56` moves its target-failure semantics into the existing `.agents/skills/code-quality-metrics/references/MetricContract.md` and keeps one pointer line.
2. Replace `plan-audit/SKILL.md:97-109` with: audit the execution card against root `AGENTS.md` `## Risk tiers`; a reviewer may escalate the tier with evidence and never silently lowers it.
3. Delete `resolve-findings/SKILL.md:70-72` and cite root `AGENTS.md` `## Diagnosis Discipline`. Compress the Fix Workflow at `:45-59` to about three lines covering confirm root cause, apply the smallest conforming fix, check affected sites. The conformance-plus-`non_structural` gate and the `PLAN-DELTA` refusal at `:36-41` are kept verbatim — they are this skill's own refusal contract.
4. Move `glsl-review/SKILL.md:48-72` `## Correctness Checks` into the existing `.agents/skills/glsl-review/references/shader-footguns.md`, cited from a one-line trigger in `## Workflow`. `## Broken Engine Contracts` (`:40-46` — the `inverse()` ban, scalar layout, descriptor-set roles) stays inline; it is repository-specific and always applies.
5. Cut `codex-review/SKILL.md:87-98` down to the interruption-fallback rule at `:92-96`, with one sentence citing root `AGENTS.md` for the manager's decide-once and reject-speculative-findings rules.
6. Cut `adversarial-review/SKILL.md` from its current 117 lines to roughly 80 by deleting the method steps at `:48-67` and the evidence rules at `:68-89`, keeping the artifact-type coverage list in Method step 3 at `:57-63` inline because it defines what the review must cover, not how to review.

Not in this plan, and deliberately so: deleting `adversarial-review` outright. That would require a coupled root `AGENTS.md` Change Workflow Step 5 edit and is a user decision, not an implementation choice.

## Critical files

- `.agents/skills/repo-code-review/SKILL.md` — `## Workflow` step 1 (`:63-81`).
- `.agents/skills/repo-code-review/references/metrics-protocol.md` — new.
- `.agents/skills/code-quality-metrics/SKILL.md` — `:48-56`.
- `.agents/skills/code-quality-metrics/references/MetricContract.md` — receives the failure-semantics contract.
- `.agents/skills/plan-audit/SKILL.md` — `## Audit` `:97-109`.
- `.agents/skills/resolve-findings/SKILL.md` — `## Fix Workflow` `:45-59` and `:70-72`.
- `.agents/skills/glsl-review/SKILL.md` — `## Correctness Checks` `:48-72`, and the `## Workflow` line that cites the reference.
- `.agents/skills/glsl-review/references/shader-footguns.md` — receives the moved checks.
- `.agents/skills/codex-review/SKILL.md` — `## Manager evaluation` `:87-98`.
- `.agents/skills/adversarial-review/SKILL.md` — `## Method` `:48-67`, `## Evidence Rules` `:68-89`.
- Root `AGENTS.md` — read-only authority.

## In scope

- `.agents/skills/repo-code-review/references/metrics-protocol.md` (new) plus the replacement of `## Workflow` step 1 (`:63-81`) by the retained `Get-CodeQualityEvidence.ps1 -Mode Compare` invocation and the three lines in Design item 1.
- `code-quality-metrics/SKILL.md:48-56` replaced by a pointer, with the target-failure semantics added to `references/MetricContract.md`.
- `plan-audit/SKILL.md:97-109` replaced by the two-clause risk-tier sentence in Design item 2.
- `resolve-findings/SKILL.md:70-72` deleted with a citation of root `AGENTS.md` `## Diagnosis Discipline`, and `:45-59` compressed to about three lines.
- `glsl-review/SKILL.md:48-72` moved into `references/shader-footguns.md` behind a one-line trigger in `## Workflow`.
- `codex-review/SKILL.md:87-98` cut to the interruption-fallback rule plus one citing sentence.
- `adversarial-review/SKILL.md:48-67` and `:68-89` deleted, with `:57-63` retained inline.

## Out of scope

- Any change to review judgment: finding classes, severity, `PASS`/`NEEDS_ACTION`/`BLOCKED` thresholds, the duplication rule, the Debt Score, the advisory status of metrics, or the conditions under which a build, claim, or escalation is required.
- Root `AGENTS.md`, including Change Workflow Step 5, the risk-tier definitions, and Diagnosis Discipline.
- Deleting `adversarial-review` or changing which skills the Change Workflow dispatches.
- `resolve-findings/SKILL.md:36-41` (the conformance-plus-`non_structural` gate and `PLAN-DELTA` refusal) and `glsl-review/SKILL.md:40-46` (`## Broken Engine Contracts`).
- `codex-review` `## Method`, `## Inputs`, `## Fallback`, and `## Notes`.
- Any change to `Invoke-CodeQualityMetrics.ps1`, the `broken-engine-code-quality-metrics/v2` schema, or any other bundled script.
- The external-claim template blocks and handoff footers in these skills, owned by `Documents/Plans/Agents/SkillSharedBlockDedup.md`.
- Every skill frontmatter, including descriptions.
- Renaming, merging, adding, or deleting any skill.

## Coordination

- The digest-wrapper rewrite of `repo-code-review` `## Workflow` step 1 and of `code-quality-metrics` `## Snapshot`/`## Compare` already landed as commit `bf4614f8` ("Add Get-CodeQualityEvidence.ps1 digest wrapper and adopt it in repo-code-review and external-deep-analysis"), so this plan has no prerequisite left. It trims the operating-manual prose that adoption left in place and keeps its `Get-CodeQualityEvidence.ps1` entry point, never reintroducing the inline command line that commit deleted.
- The `codex-review` `## Method` steps 1-3 rewrite already landed as commit `4c8abb11` ("Add codex-review prompt builder script and adopt it in the skill"), which left `## Manager evaluation` untouched; this plan owns `## Manager evaluation` only, and the two regions stay disjoint.
- Commits `0695beba` ("Adopt Get-SessionChangeInventory across review skills and add Find-SessionDebugResidue scanner") and `bf4614f8` already edited regions of `repo-code-review`, `glsl-review`, and `adversarial-review` that are disjoint from this plan's; the implementation re-derives line numbers from headings. `Documents/Plans/Agents/SkillSharedBlockDedup.md` is still unlanded and edits further disjoint regions of `repo-code-review`, `glsl-review`, `adversarial-review`, and `resolve-findings`; whichever of the two lands second re-derives line numbers from headings.

## Risk tier and invariants

Tier 2 — scoped tool behavior across review skill documentation; no engine runtime, determinism/CRC, wire, serialization, save/replay, threading, or build/bootstrap coordination surface, and no C++ change.

Invariants: every review's judgment criteria, statuses, and blocking rules are unchanged in meaning and remain reachable from the skill by one citation; a metric operational failure still yields `NEEDS_ACTION` and never a finding; `resolve-findings` still refuses structural work and still emits `PLAN-DELTA`; `glsl-review` still applies the Broken Engine shader contracts on every dispatch; `adversarial-review` still covers every changed artifact type; bundled script behavior is unchanged; no skill entry point is renamed.

## Acceptance criteria

- `repo-code-review/SKILL.md` retains at most three lines of metrics protocol beyond the entry-point invocation, and the removed manual is present in `references/metrics-protocol.md`; the advisory rule, the `NEEDS_ACTION` rule, the parse-failure-is-not-a-sanitizer-instruction rule, and the complete-parsing requirement for PASS are all still stated or cited.
- The digest-wrapper adoption landed by commit `bf4614f8` survives: `repo-code-review/SKILL.md` `## Workflow` step 1 still invokes `.agents/skills/code-quality-metrics/scripts/Get-CodeQualityEvidence.ps1 -Mode Compare` as its only metrics entry point, and the inline `Invoke-CodeQualityMetrics.ps1` command line that commit deleted is not reintroduced in `SKILL.md` or in `references/metrics-protocol.md`.
- `code-quality-metrics/SKILL.md` carries one pointer line where `:48-56` was, and `references/MetricContract.md` holds that contract.
- `plan-audit/SKILL.md` no longer restates execution-card fields or risk-tier definitions and instead cites root `AGENTS.md`, while still permitting evidence-based escalation and forbidding silent lowering.
- `resolve-findings/SKILL.md` contains no copy of the Diagnosis Discipline authority order, its Fix Workflow is about three lines, and `:36-41` is byte-unchanged.
- `glsl-review/SKILL.md` no longer contains `## Correctness Checks`, `references/shader-footguns.md` holds it, and `## Broken Engine Contracts` is byte-unchanged.
- `codex-review/SKILL.md` `## Manager evaluation` contains only the interruption-fallback rule plus one citing sentence.
- `adversarial-review/SKILL.md` is about 80 lines and still lists the artifact types it must cover.
- Each edited `SKILL.md` measures smaller than today by `.agents/scripts/Measure-Tokens.ps1`.
- `/validate-skill` passes on every touched `SKILL.md`, and every added reference link resolves.
- No bundled script is edited and no skill entry point is renamed.

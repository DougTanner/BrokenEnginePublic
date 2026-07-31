<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-31T22:14:29.582Z","dependsOn":[]} -->
# Deduplicate Shared Blocks Across Skills

## Context

Seven blocks of text are stated authoritatively in one place and then re-pasted into other skills. Every copy is a drift hazard — a rule corrected in the owner silently stays wrong in the copies — and every copy is loaded again on the dispatch that reads it. A skill body loads only when its skill runs, so the cost is per dispatch: the external-claim request template alone is roughly 900 `bt-token-v1` on every review dispatch that carries a copy. Frontmatter descriptions (~19.5 KB, ~5,000 `bt-token-v1`) load every session and are not touched here. Root `AGENTS.md` routes to skills by name only, so no `AGENTS.md` edit is required except the one term-ownership pointer in item 7, which edits the *other* documents, not `AGENTS.md`.

The seven duplications, each with its authoritative owner:

1. **External-claim request template**, five copies. `.agents/skills/verify-external-claims/SKILL.md:20-34` (`## External Claim Requests`) owns the schema; `plan-audit/SKILL.md:111-126`, `repo-code-review/SKILL.md:284-300`, `glsl-review/SKILL.md:88-101`, `adversarial-review/SKILL.md:83-88`, and `resolve-findings/SKILL.md:74-78` re-paste it.
2. **Collection-layout auditor contract**, three copies of one script's behavior. `add-collection/SKILL.md:167-206` and `add-collection-member/SKILL.md:88-108` are byte-identical, and `add-collection/SKILL.md:32-34` chains into the member skill, so a single task loads both. `update-affected-code/SKILL.md:63-83` narrates the same script's exit codes, truncation, and JSON. The script is `.agents/scripts/Test-CollectionLayout.ps1`.
3. **Handoff footer**, seven copies. `.agents/references/subagent-reporting.md:39-49` defines `Status` / `Changed files` / `Decisive checks` / `Build required` / `Residuals` and already states that skills may extend it.
4. **Execution Context boilerplate** ("runs inside one fresh delegated reviewer; findings only; tool restrictions are prose boundaries"), five copies: `plan-audit:47-52`, `scope-review:33-40`, `repo-code-review:15-18`, `adversarial-review:15-18`, `code-style-review:9-12`.
5. **Style guide Rule 49**, stated verbatim in both `repo-code-review/SKILL.md:253-260` and `code-style-review/SKILL.md:65-72`.
6. **`New-PlanFile.ps1` invocation contract**, duplicated at `create-follow-up-plans/SKILL.md:47-57` and `save-plan/SKILL.md:23-35`.
7. **"One term per concept / plain words"**, stated three ways: root `AGENTS.md` `## Directives`, `update-claude-docs/SKILL.md:67-70`, `validate-skill/SKILL.md:35`.

## Design

Each duplication resolves to exactly one owner; every former copy becomes a short citation that keeps the caller's own decision rules. No review criterion, no script, and no skill entry point changes.

1. Each of the five callers keeps one line in place of the template: "emit one single-claim request per `/verify-external-claims`; a pending verdict is `NEEDS_ACTION`." The caller's own rules about when to raise a claim stay.
2. Add `.agents/references/collection-layout-auditor.md` holding the `Test-CollectionLayout.ps1` contract now duplicated: invocation, exit codes, truncation, and JSON shape. `.agents/references/` is the existing cross-skill reference home (`subagent-reporting.md` already lives there), and all three consumers are skills, so the reference goes there rather than into `Engine/Source/Frame/Collections/AGENTS.md`. Each of the three skills keeps a two-line invocation stub plus the citation, and keeps whatever it alone decides about the auditor's result.
3. Seven skills reduce their output block to their own extension fields plus a citation of `.agents/references/subagent-reporting.md`: `plan-audit:141-152`, `adversarial-review:107-112`, `update-affected-code:102-111`, `verify-external-claims:94-97`, `glsl-review` (`### Recommendation` handoff lines), `repo-code-review` (`### Recommendation` handoff lines), and `update-vcxproj`. A skill whose block adds no field beyond the shared five replaces the block entirely with the citation.
4. Add one sentence to `.agents/references/subagent-reporting.md` stating the delegated-reviewer execution context and that tool restrictions in a skill are prose boundaries, not host enforcement. The five skills cite it and keep only what is specific to them.
5. `repo-code-review` owns Rule 49. `code-style-review:47-54` becomes "Rule 49 forwarding findings are routed, not auto-fixed — see `/repo-code-review`."
6. Add `.agents/references/new-plan-file.md` holding the `New-PlanFile.ps1` invocation contract, the `-DependsOn` single-token rule, the `broken-engine-new-plan-file/v1` result parsing, and the exit `0`/`1`/`2` handling including "a validation failure leaves the written file in place." `create-follow-up-plans` and `save-plan` each keep the command line they run plus the citation.
7. Root `AGENTS.md` `## Directives` stays authoritative. `update-claude-docs/SKILL.md:67-70` and `validate-skill/SKILL.md:35` point at it and keep only their own additions (`update-claude-docs` keeps its documentation-specific vocabulary — hub/detail, Collection, Frame, EWNS).

## Critical files

- `.agents/references/subagent-reporting.md` — gains the execution-context sentence; handoff block unchanged.
- `.agents/references/collection-layout-auditor.md` — new.
- `.agents/references/new-plan-file.md` — new.
- `.agents/skills/verify-external-claims/SKILL.md` — `## External Claim Requests` unchanged as owner; `## Report` handoff lines trimmed.
- `.agents/skills/plan-audit/SKILL.md` — `:111-126`, `:141-152`, `## Execution Context`.
- `.agents/skills/repo-code-review/SKILL.md` — `## External Claim Requests` `:284-300`, `### Public state and forwarding APIs` `:253-260`, intro `:15-18`, `### Recommendation` handoff lines.
- `.agents/skills/glsl-review/SKILL.md` — `## External Claim Requests` `:88-101`, `### Recommendation` handoff lines.
- `.agents/skills/adversarial-review/SKILL.md` — `:83-88`, `:107-112`, intro `:15-18`.
- `.agents/skills/resolve-findings/SKILL.md` — `:74-78`.
- `.agents/skills/scope-review/SKILL.md` — `## Execution Context` `:33-40`.
- `.agents/skills/code-style-review/SKILL.md` — intro `:9-12`, `:65-72`.
- `.agents/skills/add-collection/SKILL.md` — `### Collection-layout auditor` `:167-206`.
- `.agents/skills/add-collection-member/SKILL.md` — `## Collection-layout auditor` `:88-108`.
- `.agents/skills/update-affected-code/SKILL.md` — `:63-83`, `:102-111`.
- `.agents/skills/update-vcxproj/SKILL.md` — handoff block.
- `.agents/skills/create-follow-up-plans/SKILL.md` — `:47-57`.
- `.agents/skills/save-plan/SKILL.md` — `:23-35`.
- `.agents/skills/update-claude-docs/SKILL.md` — `:67-70`.
- `.agents/skills/validate-skill/SKILL.md` — `:35`.
- `.agents/scripts/Test-CollectionLayout.ps1`, `.agents/scripts/New-PlanFile.ps1` — read only; behavior unchanged.
- Root `AGENTS.md` — read only.

## In scope

- The five external-claim template blocks listed in Critical files, each replaced by the one-line citation in Design item 1.
- `.agents/references/collection-layout-auditor.md` (new) plus the three consumer regions reduced to a two-line invocation stub and citation.
- The seven handoff blocks listed in Design item 3, reduced to extension fields plus citation.
- The execution-context sentence added to `.agents/references/subagent-reporting.md` plus the five skill regions replaced by a citation.
- `code-style-review/SKILL.md:65-72` replaced by the one-line Rule 49 forwarding pointer.
- `.agents/references/new-plan-file.md` (new) plus the two caller regions reduced to command line and citation.
- `update-claude-docs/SKILL.md:67-70` and `validate-skill/SKILL.md:35` replaced by pointers to root `AGENTS.md` `## Directives`.

## Out of scope

- Any change to review or audit judgment: finding classes, severity, `PASS`/`NEEDS_ACTION`/`BLOCKED` thresholds, style rules, collection invariants, or when a claim, auditor run, or build is required.
- Root `AGENTS.md`; `.agents/references/subagent-reporting.md` beyond the single added sentence; `verify-external-claims/SKILL.md:20-34`; `repo-code-review/SKILL.md:253-260`.
- Any change to `Test-CollectionLayout.ps1`, `New-PlanFile.ps1`, or any other bundled script.
- `external-diagnose-bug`'s handoff block, owned by `Documents/Plans/Agents/ExternalAnalysisFamilyConsolidation.md`.
- The handoff blocks of `implement-plan`, `resolve-findings`, `code-style-review`, `next-plan-review`, and `session-audit`.
- Body trimming of any skill beyond the enumerated regions; that is owned by the sibling body-trim plans.
- Frontmatter of every skill, including descriptions.
- Renaming, merging, adding, or deleting any skill.

## Coordination

`Documents/Plans/Agents/ReviewChainSkillBodyTrim.md`, `Documents/Plans/Agents/PlanLifecycleSkillBodyTrim.md`, and `Documents/Plans/Agents/DomainSkillBodyTrim.md` edit some of the same `SKILL.md` files in regions disjoint from the ones listed above. Whichever lands second re-derives line numbers from headings rather than from the line numbers cited here. Commits `0695beba` ("Adopt Get-SessionChangeInventory across review skills and add Find-SessionDebugResidue scanner"), `4c8abb11` ("Add codex-review prompt builder script and adopt it in the skill"), and `bf4614f8` ("Add Get-CodeQualityEvidence.ps1 digest wrapper and adopt it in repo-code-review and external-deep-analysis") already landed further disjoint edits to several of these files; the implementation likewise re-derives line numbers from headings rather than trusting the numbers cited here.

## Risk tier and invariants

Tier 2 — scoped tool behavior across skill documentation and two new shared references; no engine runtime, determinism/CRC, wire, serialization, save/replay, threading, or build/bootstrap coordination surface, and no C++ change.

Invariants: every deduplicated rule exists exactly once and is reachable from each former copy by a citation; no caller loses a decision it alone makes; handoff blocks still retain `Build required` and keep `Residuals` last; the collection-layout auditor's exit codes and blocking behavior read the same from every consumer; bundled script behavior is unchanged; no skill entry point is renamed.

## Acceptance criteria

- The external-claim request schema appears once in the repository, in `verify-external-claims/SKILL.md`, and each of the five former copies is a single citation line naming the pending-verdict rule.
- `Test-CollectionLayout.ps1`'s exit codes, truncation, and JSON shape appear once, in `.agents/references/collection-layout-auditor.md`; all three consumers cite it and none restates them.
- The five shared handoff lines appear once, in `.agents/references/subagent-reporting.md`; each of the seven trimmed skills shows only its extension fields, still retains `Build required`, and keeps `Residuals` last.
- The execution-context sentence appears once in `.agents/references/subagent-reporting.md` and the five skills cite it.
- Rule 49's text appears once, in `repo-code-review`; `code-style-review` carries only the forwarding pointer.
- The `New-PlanFile.ps1` contract appears once in `.agents/references/new-plan-file.md`; both callers keep their command line and cite it.
- `update-claude-docs` and `validate-skill` point at root `AGENTS.md` for the one-term rule and restate none of it.
- `/validate-skill` passes on every touched `SKILL.md`, and every added reference link resolves.
- No skill directory or `name` value changes, and no bundled script is edited.

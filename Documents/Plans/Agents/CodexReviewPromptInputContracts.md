<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-02T01:11:52.654Z","dependsOn":[]} -->
# Codex Review Prompt Input Contracts

## Context

`.agents/skills/codex-review/scripts/New-CodexReviewPrompt.ps1` assembles every delegated review prompt but enforces none of the assigned skills' input contracts. Three recurring failures across four audited landings:

1. Missing target manifest for `/repo-code-review`. The skill requires a supplied `broken-engine-code-quality-target-manifest/v1` and forbids the reviewer rebuilding it (`.agents/skills/repo-code-review/SKILL.md:26-50`), but the prompt builder never generates or embeds one. First C++ review dispatches blocked in three separate sessions: Claude `050f3482-b5b9-46a7-a3a3-d79b81a3c85e` (landing `000311fd`, 19:07:41Z, 24,515 tokens wasted, manager hand-ran `-EmitTargetManifest` and re-dispatched), Codex `019fbea8-abbe` (landing `3ebf662f`, twice — 19:42:42Z and again in the 00:07Z overlap round, with a manager scratch-file patch/delete cycle each time), and Claude `2180aa65` (landing `a7280bc9`, 21:03:02Z, one wasted headless run). The skill also says the manifest comes from a stdout-only run that "writes no file" while `code-quality-metrics` `Get-CodeQualityEvidence.ps1` accepts `-TargetManifest` only as a file path — the mismatch that forces the scratch files.
2. Unvalidated `-AssignedSkill`. The parameter (line 11) is substituted into the prompt (line 328) with no existence check; session `050f3482` dispatched `-AssignedSkill acceptance-table-review`, which names no skill, and the review produced a correct result only because the scope file carried the full contract.
3. `-Head` optional for `/verify-changes`. A verification prompt generated with `-Baseline` only against an uncommitted tree returns a guaranteed procedural `BLOCKED` (`landing.reviewed: []`): Claude session `128eef4b` (landing `301ad036`, 19:00:33Z) burned a 144 s Codex run plus a manager round-trip on exactly this.

Root cause: the prompt builder treats per-skill required inputs as caller knowledge instead of validating or deriving them, so each gap fires as a wasted headless review run.

## Design

1. When `-AssignedSkill` is `repo-code-review`, the prompt builder runs `.agents/scripts/Get-SessionChangeInventory.ps1 -EmitTargetManifest` with its existing `-Baseline` (propagating `-UntrackedPath` as `-IncludeUntracked`), writes the manifest to a sibling file of `-PromptPath`, embeds the manifest path and bytes in the prompt's evidence section, and reports the manifest path in the receipt. A blocked manifest run blocks prompt creation with the inventory's code. Update `repo-code-review/SKILL.md`'s manifest-sourcing sentence to name the prompt-supplied manifest file, resolving the writes-no-file/path-parameter mismatch; `Get-CodeQualityEvidence.ps1` keeps its file-path parameter.
2. Validate `-AssignedSkill` against `.agents/skills/<name>/SKILL.md` existence and block with a new `prompt.assigned-skill-unknown` code otherwise. Preserve the documented descriptive-role escape (roles with no skill file, such as the Tier-2 coherence review) behind an explicit switch, e.g. `-AdHocRole`, so an unknown name is always a deliberate choice.
3. When `-AssignedSkill` is `verify-changes`, require `-Head` and a clean working tree for the named paths; block with a new `prompt.head-required` code otherwise.

## Critical files

- `.agents/skills/codex-review/scripts/New-CodexReviewPrompt.ps1` — parameter validation, manifest generation, receipt fields.
- `.agents/skills/codex-review/scripts/Test-CodexReviewPromptFixtures.ps1` — fixtures for the three new outcomes.
- `.agents/skills/codex-review/SKILL.md` — blocked-code list in `## Method` step 2 and the `-AdHocRole` escape.
- `.agents/skills/repo-code-review/SKILL.md` — manifest-sourcing sentences only.
- `.agents/scripts/Get-SessionChangeInventory.ps1` — read-only manifest producer.

## In scope

- `New-CodexReviewPrompt.ps1`: the three contract checks and manifest generation/embedding described above.
- `Test-CodexReviewPromptFixtures.ps1`: fixture coverage for manifest generation, `prompt.assigned-skill-unknown`, `-AdHocRole`, and `prompt.head-required`.
- `codex-review/SKILL.md` and `repo-code-review/SKILL.md`: the matching contract sentences, each stated once.

## Out of scope

- Any change to `Get-SessionChangeInventory.ps1`, `Get-CodeQualityEvidence.ps1`, the manifest schema, or review-judgment criteria.
- The prompt template's fixed wording beyond naming the manifest evidence section.
- The baseline SHA wording owned by `Documents/Plans/Agents/CodexReviewInputsBaselineSha.md`.
- Skill-mirror path resolution owned by `Documents/Plans/Agents/SkillScriptMirrorModuleResolution.md`.

## Risk tier and invariants

Tier 2 — scoped tool behavior in the review-dispatch script; no engine runtime, determinism, wire, serialization, threading, or build-coordination surface.

Invariants: prompt assembly stays script-owned with no diff bytes entering the manager session; existing successful invocation forms keep working unchanged; every new failure is a typed blocked code, never a silent degradation.

## Acceptance criteria

- A `repo-code-review` prompt generated from a C++ change embeds a manifest the reviewer can consume without manager intervention, and the previously observed first-run `BLOCKED` on a missing manifest cannot occur from a successful prompt build.
- `-AssignedSkill` naming no skill blocks with `prompt.assigned-skill-unknown` unless `-AdHocRole` is passed.
- A `verify-changes` prompt without `-Head` blocks with `prompt.head-required`.
- `Test-CodexReviewPromptFixtures.ps1` passes; `/validate-skill` passes on both edited SKILL.md files.

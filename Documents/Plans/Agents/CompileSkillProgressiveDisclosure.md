<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-31T22:14:30.739Z","dependsOn":[]} -->
# Split the Compile Skill by Progressive Disclosure

## Context

`.agents/skills/compile/SKILL.md` is ~7,556 `bt-token-v1` and loads in full on every `builder` dispatch, which is the most frequent delegation in the repository. Two of its largest sections are conditional and are dead weight on most dispatches:

- `## Select runtime data mode` (`SKILL.md:107-168`) applies only to a game build (BrokenEngineSandbox client or server) whose changed paths hit a Local data path trigger. A ThirdParty, DataPacker, WorktreeCli, AgentHarness, or selective-file build never reaches it.
- `## Explicit Microsoft PREfast verification mode` (`:170-193`) applies only when an approved plan explicitly requires PREfast verification, and the skill already states that authorization is never inferred from a routine compile.

Two smaller items in the always-read part are duplicated or redundant:

- `:174-179` restates the Microsoft-code-analysis two-path explanation that `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md` already owns in its "Microsoft code analysis" bullet (around line 23), including the `RunCodeAnalysis` split, the `EnablePREfast` gate, `CodeAnalysisTreatWarningsAsErrors`, and the `CodeAnalysisNeverReportRuleErrors` prohibition.
- The `Blocking codes:` enumeration at `:80` lists codes `Resolve-CompileContext.ps1` prints in its own `code` field, while `:72` already states the operative rule: any nonzero exit stops the build, and the exact `code` and `message` are reported.

Estimated saving: ~3,450 `bt-token-v1` off every `builder` dispatch. Root `AGENTS.md` routes to skills by name only, so no `AGENTS.md` edit is required.

## Design

Move both conditional sections into `.agents/skills/compile/references/`, each replaced in `SKILL.md` by one trigger sentence that states the condition and the reference path, so a dispatch that does not meet the condition never loads the section. This is a relocation: the moved text is unchanged apart from the heading level and any cross-reference needed to keep it readable on its own.

- `references/runtime-data-mode.md` receives `## Select runtime data mode` verbatim. `SKILL.md` keeps one sentence: a game build whose `triggerMatches` is non-empty must select the runtime data mode per that reference before building, and the reference is mandatory reading in that case, not optional background.
- `references/prefast-mode.md` receives `## Explicit Microsoft PREfast verification mode` verbatim, minus `:174-179`. `SKILL.md` keeps one sentence: PREfast verification runs only when an approved plan explicitly requires it, per that reference, and authorization is never inferred from a routine compile, rebuild, or link-error check. That prohibition stays in `SKILL.md` because it governs dispatches that never open the reference.
- `:174-179` is deleted from the moved text and replaced by a citation of `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md`, keeping only the two facts that section adds and the project file does not: report "analysis executed" and "policy passed" as separate facts, and treat a green incremental `RunNativeCodeAnalysis` run as possibly skipped unless a rebuild or regenerated merged `.nativecodeanalysis.xml` proves otherwise.
- The `Blocking codes:` enumeration at `:80` is deleted; the rule at `:72` stands as written. The neighbouring rules that name specific codes as blocking conditions rather than as an inventory — `changedPathsTruncated`, `triggerMatchesTruncated`, `deletionOnlyCandidatesTruncated`, and `output.capacity-exceeded` — stay.

`compile` has no `references/` directory today; this change creates it.

## Critical files

- `.agents/skills/compile/SKILL.md` — `:72`, `:80`, `## Select runtime data mode` `:107-168`, `## Explicit Microsoft PREfast verification mode` `:170-193`.
- `.agents/skills/compile/references/runtime-data-mode.md` — new.
- `.agents/skills/compile/references/prefast-mode.md` — new.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md` — read-only owner of the code-analysis explanation.
- `.agents/skills/compile/scripts/Resolve-CompileContext.ps1` — read-only; behavior unchanged.

## In scope

- Create `.agents/skills/compile/references/runtime-data-mode.md` holding the relocated `## Select runtime data mode` text, and replace that section in `SKILL.md` with the one trigger sentence described in Design.
- Create `.agents/skills/compile/references/prefast-mode.md` holding the relocated `## Explicit Microsoft PREfast verification mode` text without `:174-179`, and replace that section in `SKILL.md` with the one trigger sentence plus the retained no-inferred-authorization prohibition.
- In the relocated PREfast text, replace `:174-179` with the citation of `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md` plus the two retained facts named in Design.
- Delete the `Blocking codes:` enumeration at `SKILL.md:80`.

## Out of scope

- Any change to build behavior, commands, parameters, MSBuild properties, target order, or configuration selection.
- Any change to the AgentTools trigger, the immutable prebuilt WorktreeCli policy, the short-lived operation lock, worktree provisioning and lifecycle validation, target serialization, or synchronous foreground execution — those rules govern build and bootstrap coordination that can block other sessions, and touching them would escalate the change to Tier 3.
- Any change to `Resolve-CompileContext.ps1`, `New-DataOracleReceipt.ps1`, or any other bundled script or fixture.
- Any edit to `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md`, the vcxproj files, `BrokenEngineAnalysis.ruleset`, or `.clang-tidy`.
- `compile`'s frontmatter, including its description.
- Root `AGENTS.md`, which routes by skill name only.
- Every other `SKILL.md` section: `## Structured build result`, `## AgentTools trigger`, `## Determine what to build` apart from `:80`, `## Full-build commands`, `## Selective file compile`, and `## Report results`.

## Risk tier and invariants

Tier 2 — scoped tool behavior: how the build skill's own instructions are disclosed, with no change to what is built or how. The build/bootstrap coordination rules that would make this Tier 3 are explicitly out of scope and are not moved.

Invariants: the data-mode selection rules and the PREfast rules still apply in full whenever their condition holds, and a dispatch meeting the condition is told to read the reference before building; PREfast authorization is still never inferred from a routine build, and that prohibition remains in the always-loaded body; every blocking condition still stops the build with its exact `code` and `message`; bundled script behavior is unchanged; no skill entry point is renamed.

## Acceptance criteria

- `.agents/skills/compile/SKILL.md` measures at least 3,000 `bt-token-v1` smaller than today by `.agents/scripts/Measure-Tokens.ps1`, and the two references together carry the removed text.
- The relocated data-mode and PREfast text is semantically unchanged from the current sections, except for the deleted `:174-179` block and its replacement citation.
- `SKILL.md` states each condition and its reference path in one sentence, so a `builder` dispatched for ThirdParty, DataPacker, WorktreeCli, AgentHarness, or a selective file compile reads neither reference.
- `SKILL.md` still prohibits inferring PREfast authorization from a routine compile, rebuild, or link-error check.
- `SKILL.md` no longer contains the `Blocking codes:` enumeration, and the nonzero-exit rule at `:72` is unchanged.
- A game build with a Local trigger match and a PREfast run both remain executable end to end from `SKILL.md` plus the cited reference, producing the same commands as today.
- `/validate-skill` passes on the edited `SKILL.md`, and both reference links resolve.
- No bundled script is edited and no skill entry point is renamed.

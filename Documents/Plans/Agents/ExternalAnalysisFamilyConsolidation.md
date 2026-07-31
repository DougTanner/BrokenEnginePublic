<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-31T22:14:41.361Z","dependsOn":["Documents/Plans/Agents/SkillDescriptionTrim.md"]} -->
# Consolidate the External-Analysis Family

## Context

Five skills form the external-analysis family: `external-architecture-review`, `external-refactor-clean`, `external-deep-analysis`, `external-diagnose-bug`, `external-design-interface`. A skill body loads in full on every dispatch; frontmatter descriptions (~19.5 KB, ~5,000 `bt-token-v1`) load every session and are not rewritten here except for the merged skill's own description. Root `AGENTS.md` names none of these skills, so no `AGENTS.md` edit is required.

The family's largest cost is a boundary that exists only because the two code-analysis skills are separate packages. Roughly a third of each body polices that boundary against the other: `external-refactor-clean/SKILL.md:12`, `:52-56`, `:91-94`, `:142-145`, `:154-157` and `external-architecture-review/SKILL.md:15`, `:122-123`. On merge that prose has nothing left to police and disappears. `external-architecture-review` additionally carries a generic cohesion lens (`### Lens D`, `:52-60`), a self-restating line at `:81`, and a 39-line output template (`:95-133`).

Three further regions restate work other skills own: `external-deep-analysis` Phases 4-5 (`:116-146`) restate `/create-follow-up-plans`, `/verify-changes`, and `/finalize-changes`; `external-diagnose-bug:66-87` spells out generic reproduce-and-rank method; `external-skill-creator` carries two illustrative examples and a `references/client-compatibility.md` that duplicates `.agents/skills/validate-skill/references/frontmatter-schema.md`. `external-design-interface:84-89` restates root `AGENTS.md` Change Workflow Steps 1-2 tiering.

## Design

### 1. Merge the two code-analysis skills

Create one skill package `.agents/skills/analyze-code/` (`name: analyze-code`) and delete `.agents/skills/external-architecture-review/` and `.agents/skills/external-refactor-clean/`. This is the plan's one entry-point rename; its coupled inbound-reference updates are listed below and the complete set is confirmed by running `.agents/skills/validate-skill/scripts/Find-SkillInboundReferences.ps1` for both retired names before the change is considered complete.

Content:

- Transferred intact: `external-refactor-clean`'s inspection rubric (`:70-84`) and repository-authority mapping (`:96-111`), and `external-architecture-review`'s lenses A, B, C, and E (dependencies and layering; simulation and threading; client/server and data shape; ThirdParty replacement).
- Deleted: every boundary-policing region listed in Context; `### Lens D` (`:52-60`), whose cohesion and generation-residue material is generic and already covered by the transferred rubric; and the self-restating line at `:81`.
- Collapsed: the 39-line output template at `:95-133` becomes about four lines naming the required output sections.
- Retained routing: oversized files still route to `/reduce-file`.
- One merged `.agents/skills/analyze-code/LICENSE` replaces the two upstream files. They are not two different licenses: each is a `Third-Party Notices` file, and the only block they share — the Seth Hobson upstream MIT text under `Copyright (c) 2024` — is byte-identical in both, so keeping two files would duplicate that text and split one package's notices across two names. The merged file carries the union in this order: the `wshobson/commands, arch-review`, `mattpocock/skills, improve-codebase-architecture`, and `wshobson/commands, refactor-clean` attribution entries copied verbatim (source URL, license URL, and adaptation paragraph each unchanged), then the Seth Hobson upstream MIT text once and the Matt Pocock upstream MIT text once. No attribution line, URL, or license paragraph is dropped, reworded, or merged with another.
- One `agents/openai.yaml` keeps the current no-implicit-invocation policy of both packages.
- The description keeps its negative-trigger prose ("only on explicit request or when an explicitly documented parent workflow chains to it; not for routine code changes or general code questions"). `disable-model-invocation` must stay off so `external-deep-analysis` can chain natively, which makes that prose the only implicit-selection suppressor. `Documents/Plans/Agents/SkillDescriptionTrim.md` deliberately excludes these descriptions for that reason.

Coupled edits:

- `external-deep-analysis/SKILL.md` Phase 1 (`:70-80`) and Phase 2 (`:81-91`) become one phase invoking `analyze-code`.
- `external-skill-creator/references/client-compatibility.md:17` — the native-chain exception list names `analyze-code` instead of the two retired skills.

### 2. `external-deep-analysis` Phases 4-5

Replace `:116-146` with two routing sentences: proven out-of-scope leftovers go to `/create-follow-up-plans`; acceptance and landing follow root `AGENTS.md` Change Workflow Steps 7 and 8 through `/verify-changes` and `/finalize-changes`. Phase 0 (`:36-69`) is untouched here; it already carries the landed `Get-CodeQualityEvidence.ps1` digest-wrapper adoption (commit `bf4614f8`, "Add Get-CodeQualityEvidence.ps1 digest wrapper and adopt it in repo-code-review and external-deep-analysis").

### 3. `external-diagnose-bug`

Compress the generic method at `:66-87` (reproduce and minimize; rank hypotheses) to about six lines. Kept unchanged: the repository signal sources at `:38-52`, the `[DEBUG-a4f2]` instrumentation marker convention at `:93-98`, and the handoff block at `:129-140`.

### 4. `external-skill-creator`

Delete the weak-versus-strong description example at `:38-41` and the upstream `feat(auth)` commit example at `:49-54`. In `references/client-compatibility.md`, delete the sections duplicating `.agents/skills/validate-skill/references/frontmatter-schema.md` (`:9`, `:31-36`, `:38`), keeping only the Claude prompt-substitution facts at `:21-25` and the native-chain exception statement at `:17` as updated by item 1. `SKILL.md:30` — the PowerShell-versus-Python rule that root `AGENTS.md` line 9 points at — is preserved exactly where it is; moving it would require repointing `AGENTS.md` in the same change and is not attempted.

### 5. `external-design-interface`

Delete `:84-89`, which restates root `AGENTS.md` Change Workflow Steps 1-2 tiering, and cite `AGENTS.md` instead. Reorganize the body block that `Documents/Plans/Agents/SkillDescriptionTrim.md` moves out of the frontmatter (the ask-the-user question and the do-not list) so it reads as one short trigger-and-exclusion section rather than transplanted frontmatter.

## Critical files

- `.agents/skills/analyze-code/SKILL.md`, `.agents/skills/analyze-code/agents/openai.yaml`, and `.agents/skills/analyze-code/LICENSE` (the single merged notices file) — new.
- `.agents/skills/external-architecture-review/` and `.agents/skills/external-refactor-clean/` — deleted.
- `.agents/skills/external-deep-analysis/SKILL.md` — Phases 1-2 (`:70-91`), Phases 4-5 (`:116-146`).
- `.agents/skills/external-diagnose-bug/SKILL.md` — `:66-87`.
- `.agents/skills/external-skill-creator/SKILL.md` — `:38-41`, `:49-54`.
- `.agents/skills/external-skill-creator/references/client-compatibility.md` — `:9`, `:17`, `:31-36`, `:38`.
- `.agents/skills/external-design-interface/SKILL.md` — `:84-89` and the relocated description block.
- `.agents/skills/validate-skill/references/frontmatter-schema.md` — read-only owner of the schema.
- `.agents/skills/validate-skill/scripts/Find-SkillInboundReferences.ps1` — run to confirm the inbound set; unchanged.

## In scope

- Create `.agents/skills/analyze-code/` with the merged `SKILL.md`, `agents/openai.yaml`, and the single merged `LICENSE` specified in Design item 1, containing exactly the transferred, deleted, and collapsed content listed in Design item 1; delete both retired skill directories.
- `external-deep-analysis/SKILL.md`: Phases 1-2 merged into one phase invoking `analyze-code`; Phases 4-5 replaced by the two routing sentences.
- `external-skill-creator/references/client-compatibility.md:17`: native-chain exception list updated to `analyze-code`.
- `external-diagnose-bug/SKILL.md:66-87` compressed to about six lines.
- `external-skill-creator/SKILL.md:38-41` and `:49-54` deleted.
- `external-skill-creator/references/client-compatibility.md:9`, `:31-36`, and `:38` deleted, keeping `:21-25`.
- `external-design-interface/SKILL.md:84-89` deleted with a citation of root `AGENTS.md` Steps 1-2, and the relocated description block reorganized into one trigger-and-exclusion section.
- Any further live inbound reference to the two retired skill names that `Find-SkillInboundReferences.ps1` reports.

## Out of scope

- `external-deep-analysis` Phase 0 (`:36-69`) and Phase 3, and every metric-evidence rule; Phase 0 already carries the landed digest-wrapper adoption from commit `bf4614f8` and stays byte-unchanged here.
- `external-diagnose-bug:38-52`, `:93-98`, and `:129-140`.
- `external-skill-creator/SKILL.md:30` and root `AGENTS.md`, including the pointer at its line 9.
- `.agents/skills/validate-skill/` in every respect, including `references/frontmatter-schema.md`.
- `Documents/Investigations/Documents/ReviewRubricReferences.md`, which records a past state and is not a live routing reference.
- Adding `disable-model-invocation` to the merged skill, or changing its implicit-invocation policy.
- Any change to analysis judgment: finding classes, severity, the Debt Score, verification rules, or when a review is required.
- Any change to `.agents/references/subagent-reporting.md` or to any bundled script, including `Find-SkillInboundReferences.ps1`.
- The frontmatter description of every skill except the merged one.
- Renaming, merging, adding, or deleting any skill other than the one merge stated above.

## Coordination

- The digest-wrapper adoption in `external-deep-analysis` Phase 0 already landed as commit `bf4614f8`. The regions this plan edits are disjoint from it; the implementation still re-derives line numbers from phase headings.
- `Documents/Plans/Agents/SkillSharedBlockDedup.md` excludes `external-diagnose-bug`'s handoff block precisely because this plan keeps it unchanged.

## Risk tier and invariants

Tier 2 — scoped tool behavior across the analysis skill family, including one entry-point rename with its inbound references updated in the same change. No engine runtime, determinism/CRC, wire, serialization, save/replay, threading, or build/bootstrap coordination surface, and no C++ change.

Invariants: every analysis capability reachable today stays reachable — the merged skill covers both the cross-file shape lenses and the in-function rubric; no inbound reference names a skill that no longer exists; the merged skill is still not selected implicitly for routine code changes or general code questions, and `external-deep-analysis` can still chain to it natively; upstream license text is preserved; `external-skill-creator`'s PowerShell-versus-Python rule stays where root `AGENTS.md` points; bundled script behavior is unchanged.

## Acceptance criteria

- `.agents/skills/analyze-code/` exists with the merged body, one `agents/openai.yaml`, and one `LICENSE` holding all three attribution entries plus the Seth Hobson and Matt Pocock upstream MIT texts once each; neither retired directory remains.
- `Find-SkillInboundReferences.ps1` reports no live reference to `external-architecture-review` or `external-refactor-clean` outside `Documents/Investigations/` and `Documents/Plans/`.
- The merged `SKILL.md` contains the transferred rubric and repository-authority mapping and lenses A, B, C, and E, contains no boundary-policing prose, no Lens D, and an output section of about four lines, and is smaller than the sum of the two current bodies by at least a third measured with `.agents/scripts/Measure-Tokens.ps1`.
- The merged description still states the negative triggers, and the package does not set `disable-model-invocation`.
- `external-deep-analysis` invokes `analyze-code` in one phase and its Phases 4-5 are two routing sentences; Phase 0 is byte-unchanged by this plan.
- `external-diagnose-bug:66-87` is about six lines, and `:38-52`, `:93-98`, and `:129-140` are byte-unchanged.
- `external-skill-creator/SKILL.md` contains neither illustrative example, `SKILL.md:30` is byte-unchanged, and `references/client-compatibility.md` contains only the prompt-substitution facts plus the updated native-chain statement, restating no part of `frontmatter-schema.md`.
- `external-design-interface` no longer restates Change Workflow tiering and carries the relocated trigger-and-exclusion section once.
- `/validate-skill` passes on the merged `SKILL.md` and on every other touched `SKILL.md`, and every reference link resolves.
- No bundled script is edited.

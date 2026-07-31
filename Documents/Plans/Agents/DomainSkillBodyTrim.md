<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-31T22:14:40.167Z","dependsOn":[]} -->
# Trim Domain Skill Bodies

## Context

A skill body loads in full on every dispatch of that skill. Frontmatter descriptions (~19.5 KB, ~5,000 `bt-token-v1`) load every session and are not touched here; the `gaea2-*` skills additionally declare `disable-model-invocation`, so body size is their entire cost. Root `AGENTS.md` routes to skills by name only, so no `AGENTS.md` edit is required.

Six domain-skill regions restate their own neighbouring prose, carry a surveyed constant table on every dispatch, or spell out a procedure that only one phase of the skill needs:

1. `add-collection/SKILL.md` `## Failure-Sensitive Checklist` (`:112-156`) restates `:36-110` in checkbox form. Only two of its items encode decisions the collection-layout auditor cannot make: harness-query exposure (`:147-156`) and transfer/hydration (`:142-146`). About 700 `bt-token-v1`.
2. `add-collection-member/SKILL.md` `## Completion checklist` (`:70-84`) and the prose at `:26-68` state the same layout, CRC, persistence, transfer, and identity steps twice.
3. `gaea2-modify/SKILL.md` `## Gaea 2 conventions when adding new nodes` (`:68-127`) is roughly 1,600 `bt-token-v1` of surveyed Gaea constants — ID floors, `Version: 2` type lists, per-type enum constraints, `PortCount` rules — loaded on every modify dispatch and deep-linked from `gaea2-diagnose/SKILL.md:122` and `:133`.
4. `analyze-diagsession/SKILL.md:28-97` is the extract, symbolize, and build-evidence procedure, needed only once a capture is in hand.
5. `update-claude-docs/SKILL.md` `### Current State and Vocabulary` mixes the repository-wide one-term-per-concept rule with documentation-specific vocabulary (hub/detail at `:64`, and the Collection, Frame, and EWNS terms).
6. `reduce-file/SKILL.md:101-125` is a 25-line output template.

## Design

1. `add-collection`: collapse `## Failure-Sensitive Checklist` to the two items the auditor cannot decide — transfer/hydration (`:142-146`) and harness-query exposure (`:147-156`) — and delete the checkbox restatement of `:36-110`. The invariants prose at `:36-110` is load-bearing and is not thinned.
2. `add-collection-member`: keep `## Completion checklist` as the single operative list and thin the prose at `:26-68` to what the checklist cannot carry — the reason a step exists and the judgment it requires.
3. `gaea2-modify`: move `## Gaea 2 conventions when adding new nodes` to `.agents/skills/gaea2-shared/references/node-conventions.md`, cited from a one-line trigger in `gaea2-modify`, and retarget the two cross-skill deep links at `gaea2-diagnose/SKILL.md:122` and `:133`, plus the in-file "Per-type enum constraints" pointer left by commit `736c1d7f` at `gaea2-modify/SKILL.md:151`, to headings in the new reference. `gaea2-shared` is a script and asset library reached as `${CLAUDE_SKILL_DIR}/../gaea2-shared/`, which is why the shared reference belongs there rather than in one of the four consuming skills.
4. `analyze-diagsession`: move `:28-97` to `.agents/skills/analyze-diagsession/references/capture-forensics.md`, cited from a one-line trigger; `SKILL.md` lands at about 60 lines covering roles, what is actionable, and routing.
5. `update-claude-docs`: keep the documentation-specific vocabulary in `### Current State and Vocabulary` — hub/detail at `:64` and the Collection, Frame, and EWNS terms — and remove the surrounding restatement of the repository-wide rule. The pointer to root `AGENTS.md` `## Directives` for the one-term rule itself is owned by `Documents/Plans/Agents/SkillSharedBlockDedup.md`; this plan does not write it.
6. `reduce-file`: halve the output template at `:101-125`, keeping every heading a consumer reads and dropping the per-field narration.

## Critical files

- `.agents/skills/add-collection/SKILL.md` — `## Failure-Sensitive Checklist` `:112-156`.
- `.agents/skills/add-collection-member/SKILL.md` — `:26-68`; `## Completion checklist` `:70-84` retained.
- `.agents/skills/gaea2-modify/SKILL.md` — `## Gaea 2 conventions when adding new nodes` `:68-127`, and the "Per-type enum constraints" pointer at `:151`.
- `.agents/skills/gaea2-shared/references/node-conventions.md` — new.
- `.agents/skills/gaea2-diagnose/SKILL.md` — the deep links at `:122` and `:133`.
- `.agents/skills/analyze-diagsession/SKILL.md` — `:28-97`.
- `.agents/skills/analyze-diagsession/references/capture-forensics.md` — new.
- `.agents/skills/update-claude-docs/SKILL.md` — `### Current State and Vocabulary`, excluding `:67-70`.
- `.agents/skills/reduce-file/SKILL.md` — `:101-125`.
- `.agents/scripts/Test-CollectionLayout.ps1` — read-only; behavior unchanged.

## In scope

- `add-collection/SKILL.md:112-156` collapsed to the transfer/hydration and harness-query items only.
- `add-collection-member/SKILL.md:26-68` thinned to reasons and judgment, with `## Completion checklist` kept.
- `gaea2-modify/SKILL.md:68-127` moved to `.agents/skills/gaea2-shared/references/node-conventions.md` (new) behind a one-line trigger, and the two `gaea2-diagnose` deep links at `:122` and `:133` plus the in-file "Per-type enum constraints" pointer at `gaea2-modify/SKILL.md:151` retargeted to that reference.
- `analyze-diagsession/SKILL.md:28-97` moved to `references/capture-forensics.md` (new) behind a one-line trigger.
- `update-claude-docs/SKILL.md` `### Current State and Vocabulary` apart from `:67-70`: documentation-specific vocabulary kept, local restatement removed.
- `reduce-file/SKILL.md:101-125` halved, keeping every consumer-read heading.

## Out of scope

- Any change to collection invariants, version and identity rules, CRC, persistence, serialization, transfer, hydration, or harness-query behavior, and any change to `add-collection/SKILL.md:36-110`.
- The collection-layout auditor blocks in `add-collection` and `add-collection-member`, owned by `Documents/Plans/Agents/SkillSharedBlockDedup.md`.
- The gaea2 Python bootstrap deduplication and the body of `gaea2-modify` `## Sanity checks before stopping`, both of which already landed as commit `736c1d7f` ("Add Gaea 2 validation and dispatch wrappers"); the only edit this plan makes inside that section is the one citation repoint named in `## In scope`.
- Any change to Gaea node semantics, the per-type enum constraints themselves, the round-trip format, or any `gaea2-shared` script.
- `update-claude-docs/SKILL.md:67-70`, owned by `Documents/Plans/Agents/SkillSharedBlockDedup.md`, and root `AGENTS.md`.
- `reduce-file` thresholds, `## Qualify the Target`, and the analysis criteria.
- Any change to `.agents/scripts/Test-CollectionLayout.ps1`, `Measure-Tokens.ps1`, or any other bundled script.
- Every skill frontmatter, including descriptions.
- Renaming, merging, adding, or deleting any skill.

## Coordination

- `Documents/Plans/Agents/SkillSharedBlockDedup.md` edits the auditor blocks in `add-collection` and `add-collection-member` and the one-term pointer at `update-claude-docs:67-70`. Those regions are disjoint from this plan's; whichever lands second re-derives line numbers from headings.
- Commit `736c1d7f` ("Add Gaea 2 validation and dispatch wrappers") already rewrote `gaea2-modify` `## Sanity checks before stopping` and `gaea2-diagnose` Step 1. The landed sanity-check text at `gaea2-modify/SKILL.md:151` still points at the conventions section this plan relocates, so this plan repoints it at `.agents/skills/gaea2-shared/references/node-conventions.md` and changes nothing else in that section.
- Commit `0695beba` ("Adopt Get-SessionChangeInventory across review skills and add Find-SessionDebugResidue scanner") already edited `reduce-file` `## Qualify the Target`; that landed text is disjoint from the output template this plan halves and stays unchanged.

## Risk tier and invariants

Tier 2 — scoped tool behavior across domain skill documentation; no engine runtime, determinism/CRC, wire, serialization, save/replay, threading, or build/bootstrap coordination surface, and no C++ change.

Invariants: every collection invariant and every Gaea convention still applies and is reachable from its skill by one citation; the two checklist items the auditor cannot decide stay inline where the agent reads them before finishing; `gaea2-diagnose`'s cross-skill links still resolve to the rules they name today; `analyze-diagsession` still tells a dispatch holding a capture to read the forensics reference before reporting; bundled script behavior is unchanged; no skill entry point is renamed.

## Acceptance criteria

- `add-collection/SKILL.md` contains no checkbox restatement of `:36-110`, retains the transfer/hydration and harness-query items, and `:36-110` is unchanged.
- `add-collection-member/SKILL.md` states each completion step once, in `## Completion checklist`, with the prose carrying only reasons and judgment.
- `gaea2-modify/SKILL.md` contains no Gaea constant tables, `.agents/skills/gaea2-shared/references/node-conventions.md` holds them unchanged, and both `gaea2-diagnose` deep links and the `gaea2-modify:151` pointer resolve to headings in that file.
- `analyze-diagsession/SKILL.md` is about 60 lines and `references/capture-forensics.md` holds the extract, symbolize, and build-evidence procedure unchanged.
- `update-claude-docs/SKILL.md` retains the hub/detail, Collection, Frame, and EWNS vocabulary and no longer restates the repository-wide one-term rule around it.
- `reduce-file/SKILL.md`'s output template is about half its current length and still carries every heading a consumer reads.
- Each edited `SKILL.md` measures smaller than today by `.agents/scripts/Measure-Tokens.ps1`, with `add-collection` down by roughly 700 `bt-token-v1` and `gaea2-modify` by roughly 1,600.
- `/validate-skill` passes on every touched `SKILL.md`, and every added or retargeted reference link resolves.
- No bundled script is edited and no skill entry point is renamed.

## Notes

Residual, recorded rather than fixed here: `.agents/skills/gaea2-shared/` has no `SKILL.md` by design — it is a script and asset library reached as `${CLAUDE_SKILL_DIR}/../gaea2-shared/`, not an invocable skill. Tooling that assumes every directory under `.agents/skills/` is a validatable skill package will trip on it, and this plan adds a `references/` directory there without changing that fact.

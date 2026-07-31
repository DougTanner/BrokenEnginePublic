<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-30T23:01:30.000Z","dependsOn":["Documents/Plans/Documentation/CompressedPhraseRewrites.md"]} -->
# Formal Word Swaps

## Context

Governing objective: reduce jargon in AGENTS.md and skill files so smarter agents remain understandable to the user. Simplify language as much as possible WITHOUT losing specificity. Keep standardized terms — exactly one term per concept, defined once at the owning doc — and eliminate cases where two terms refer to the same thing or one term means several things.

This plan is the full mechanical sweep of every FORMAL finding: a rare or Latinate word replaced by the plain word that means the same thing. Replacements must preserve meaning exactly — the plain word is fitted to each sentence's grammar, never pasted blindly. In the rare case where the plain word would lose a needed distinction, the site keeps the original word and the owning doc defines it once (keep-and-define); each such case is reported in the handoff.

Site lists are the discovered sites, not necessarily all: for each word below, grep it repo-wide in tracked `*.md` files and replace every use in the targeted sense.

## Design — replacements

### Sweep words (many sites each)

1. **adjudicate/adjudication → decide/decision** — root:41; `codex-review`:64; `agent-harness`:114; `plan-audit`:112; `resolve-findings`:15; `glsl-review`:91; `verify-external-claims`:76; `external-grill-plan`:22; `create-follow-up-plans`:9.
2. **mutation/mutate → change/changed** (of branch, doc, or state) — root:54,77 ("primary will be mutated" → "primary will be changed"); root:21 and :83 mutation phrasing; `Documents/Features/AGENTS.md`:3; Save/Ui/Network AGENTS.md files; `finalize-changes`:27; `save-plan`:39; `DataPacker/Source/AGENTS.md`:17; `Tools/AgentHarness/AGENTS.md`:8; `Targets`:7.
3. **terminal → final** — root:77; `next-plan`:13,47,93; `finalize-changes`:22. Aligns with the glossary plan's "final Plan state" (decision 29).
4. **canonical → per sense** (glossary decision 10): authoritative sense → **authoritative** (root:24 and skill uses; `Platforms`:18; `Save`:16; `Network`:30; `Frame`:20; `implement-plan`:18,27; `save-plan`:39; `update-claude-docs`:67,71; `gaea2-modify`:109; `verify-changes`:76); normalized-path sense → **normalized** (`Documents/AGENTS.md`:24; `Documents/Plans/AGENTS.md`:13; `next-plan`:27; and `next-plan`:54 per its sense). Classify each site's sense before swapping.
5. **materiality/material → "whether it matters" / significant** — root:41,66; `external-grill-plan`:66; `adversarial-review`:52; `next-plan`:45. Where "material" modifies a defined workflow test (root:83 "a material diff change"), use "a diff change that matters" — same test, plain words.
6. **falsifiable/falsification → checkable / trying to disprove** — `adversarial-review`:4; `implement-plan`:90; `resolve-findings`:50; `verify-external-claims`:24; `external-grill-plan`:47; root role table "adversarial falsification" → "adversarial review that tries to disprove the change".

### One-off words

| Word | Site(s) | Replacement |
| --- | --- | --- |
| liveness | root:18 | "whether the worker is still running" |
| pin | root:38 | "lock in" (an ad-hoc `model:` cannot lock in effort) |
| proportionate | root:49 | "matching the size of the change" |
| quiescence | `compile`:42 | "no activity" / "goes idle" (fit to sentence) |
| remit | `codex-review`:44 | "scope" |
| belt-and-braces | `compile`:168 | "extra safety measure" |
| orthogonal | `next-plan-review`:187 | "independent" / "unrelated" (per sentence) |
| admissible | `next-plan-review`:215 | "allowed" |
| annunciators | `Engine/Source/Profile/AGENTS.md`:17 | "warning indicators" |
| pact | `Engine/Data/Shaders/Water/AGENTS.md`:14 | "contract" |
| save (= except) | root:117 | "except for" |
| gold-plating | root:41 | "unnecessary extra work" |
| ground truth | `gaea2-diagnose`:75 | "the actual measured values" |
| façades | `Engine/Source/AGENTS.md`:30 | "thin wrappers" |
| vertical rhythm | `Documents/AGENTS.md`:10 | "consistent vertical spacing" — unless `UserInterfaceDesign.txt` uses "vertical rhythm" as its own defined term, in which case keep-and-reference that doc |
| blast radius | `Documents/AGENTS.md`:50 | "spread of breakage" ("chance / blast radius of breakage" → "chance and spread of breakage") |
| in-flight | `Documents/AGENTS.md`:58 | "still in progress" |
| cardinality | `add-collection`:15 | "count" |
| non-invocable | `add-collection`:64 | "cannot be called" |
| atomic | `adversarial-review`:66 | "single, independently checkable" (an "atomic claim" → "a single checkable statement") |

### Findings intentionally not swapped

- **disposition → outcome** (`compile`:20) is owned by `ProcessVocabularyRenames.md` (glossary decision 9) — do not double-edit.
- **authorize/authorization** (root:31; `DataPacker/Source/AGENTS.md`:19; `Network/Server`:17): kept. "Authorize" is ordinary English and is load-bearing in the workflow's approval language; swapping to "allow" would blur user authorization against mere permission. Recorded here so the findings list is fully resolved.

## Critical files

Root `AGENTS.md`; `Documents/AGENTS.md`; `Documents/Features/AGENTS.md`; `Documents/Plans/AGENTS.md`; engine docs listed above (`Profile`, `Shaders/Water`, `Engine/Source`, `Save`, `Network`, `Frame`, `Platforms`, `DataPacker/Source`, `Tools/AgentHarness`, `Targets`); SKILL.md files for: `codex-review`, `agent-harness`, `plan-audit`, `resolve-findings`, `glsl-review`, `verify-external-claims`, `external-grill-plan`, `create-follow-up-plans`, `finalize-changes`, `save-plan`, `next-plan`, `next-plan-review`, `implement-plan`, `adversarial-review`, `update-claude-docs`, `gaea2-modify`, `gaea2-diagnose`, `verify-changes`, `compile`, `add-collection`.

## Out of scope

- C++ and GLSL code changes; code identifier renames.
- Collision renames (plans 2–3) and compressed-phrase rewrites (plan 5).
- Skill behavior changes — wording only; skill names, frontmatter triggers, and output token formats unchanged (e.g. `verify-external-claims` keeps its name; VERIFIED/REFUTED/UNRESOLVED and PASS/FAIL tokens unchanged).
- Feature plan documents under `Documents/Features/` are out of scope; the tree's `Documents/Features/AGENTS.md` guidance file itself IS in scope (the `:3` word swap above). `ThirdParty/`.

## Risk and invariants

Risk 1. Invariant: exact meaning preservation — every swap must read as the same rule after the edit; a swap that would weaken or broaden a contract sentence becomes keep-and-define and is reported. Skill files feed agent behavior: every changed `.agents/skills/*/SKILL.md` triggers `/validate-skill`. Repo-wide grep per word catches sites the scan missed.

## Acceptance criteria

- Every listed site is edited or explicitly recorded as keep-and-define / not-swapped with its reason.
- Repo-wide grep of tracked `*.md`: zero remaining uses of adjudicate/adjudication, remit, quiescence, belt-and-braces, admissible, annunciators, gold-plating, façades, cardinality, non-invocable; "terminal", "canonical", "material(ity)", "falsifi-", "mutat-" hits are each confirmed outside the targeted senses (e.g. terminal emulator, material assets).
- Changed SKILL.md files pass `/validate-skill`.

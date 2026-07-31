<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-30T23:48:00.000Z","dependsOn":["Documents/Plans/Documentation/FormalWordSwaps.md"]} -->
# De-Jargon Guardrails

## Context

Governing objective, adapted to prevention: keep future AGENTS.md and SKILL.md edits jargon-free per the standards `TerminologyGlossaryAndDecisions.md` established — simplify language as much as possible without losing specificity, exactly one term per concept, defined once at the owning doc, plain words elsewhere — so smarter agents remain understandable to the user and the de-jargon effort does not erode as documentation evolves.

The five prior Documentation plans fix the existing text. Nothing yet stops the next documentation edit from reintroducing a synonym, an undefined coined term, a formal word, or a noun-stack. The two skills that govern documentation quality are the durable enforcement points: `.agents/skills/update-claude-docs/SKILL.md` (writes and audits AGENTS.md) and `.agents/skills/validate-skill/SKILL.md` (reviews SKILL.md wording). This plan bakes concise anti-jargon rules into both, plus one directive line in root `AGENTS.md` so AGENTS.md edits made outside the doc-sync skill are covered too.

This plan depends on `FormalWordSwaps.md`, which runs last of the rename/rewrite plans: the same skill files are being edited by those plans, and the rules below reference the settled vocabulary.

Durability note: completed Plans are deleted from the tree, so the rule text below is self-contained and never references the Documentation plan files. The "established term" rules inherently cover retired terms — reintroducing a second name for a concept that already has one is the finding, whether or not the name once existed.

## Design

Three small edits. The rule text below is the deliverable — implementation is mechanical insertion, fitted only for surrounding list style. The guidance adds jargon criteria to what each skill already checks; it must not change what either skill does (no new phases, modes, outputs, or dispatch behavior).

### (a) `.agents/skills/update-claude-docs/SKILL.md` — extend `### Current State and Vocabulary`

Append these bullets to the existing list under `## Content Rules` → `### Current State and Vocabulary` (which already contains "Search the tree before inventing a near-synonym"):

- Use the one established term for each concept everywhere; never introduce a synonym for a concept that already has a name. Reintroducing a second name — including one the repository previously retired in favor of the established term — is a finding, not a style choice.
- Define a genuinely new coined term in one plain sentence at the doc that owns the concept, on first use; every other doc references the owning doc instead of redefining it.
- Prefer the plain word over the formal one when the meaning is identical: decide, not adjudicate; change, not mutate; final, not terminal; count, not cardinality.
- Write concrete actions, not abstract noun-stacks: say who does what ("the loop that keeps processing until the queue is empty"), not a compressed label ("drain loop") — unless the label is an established defined term or a code identifier.

Audit modes report violations of these bullets as findings like any other content-rule violation; no new rubric section is added.

### (b) `.agents/skills/validate-skill/SKILL.md` — one Recommended bullet in Workflow step 4

Insert alongside the existing "Recommended: keep instructions imperative, general, and concise" bullet:

- Recommended: plain wording — use the established repository term for each concept and never a synonym for one that already has a name; a genuinely new term is defined once at its owning doc, not coined inline; prefer the plain word over the formal one when meaning is identical; name concrete actions instead of abstract noun-stacks unless the stack is an established term or code identifier. Report a reintroduced synonym for an established term as a finding.

It joins the existing Recommended tier deliberately: step 5 already forbids promoting ordinary quality advice to Critical unless discovery or invocation is concretely incorrect, and jargon never is.

### (c) Root `AGENTS.md` — one directive line

Add one bullet to the `## Directives` list:

- One term per concept: use the established repository term; define a genuinely new term once at its owning doc and reference it elsewhere; prefer plain words over formal ones.

No further sites. This is the single additional load-bearing location: root Directives govern all work, including AGENTS.md edits that never pass through `update-claude-docs`.

## Critical files

- `.agents/skills/update-claude-docs/SKILL.md`
- `.agents/skills/validate-skill/SKILL.md`
- Root `AGENTS.md` (one directive bullet)

## Out of scope

- Every rename, rewrite, definition, or site fix owned by the other five Documentation plans — this plan renames nothing.
- C++ and GLSL code changes; code identifier renames.
- New skills, new review workflows, new audit phases or modes, new report formats — only checklist text inside existing structures.
- Changing any EXISTING skill step, output format, or behavior. Adding the drafted jargon criteria to `update-claude-docs` and `validate-skill` is this plan's deliverable and is in scope; what is out of scope is altering anything the skills already do — triggers, frontmatter, existing workflow steps, dispatch behavior, output contracts, and the Critical/Recommended classification scheme all stay as they are. After this plan the skills do exactly what they do today, plus the new jargon checks.
- `Documents/Features/` tree and `ThirdParty/`.

## Risk and invariants

Risk 1: wording-only additions, but both files feed agent behavior. Invariants: (a) the added text only extends what the skills check — a diff touching any workflow step semantics, mode behavior, or output format other than the three insertions above is out of bounds; (b) the rule text is self-contained and references no Plan file; (c) the `validate-skill` addition stays Recommended-tier. Both changed SKILL.md files trigger `/validate-skill` (which for `validate-skill` itself includes its self-validation bootstrap). Root `AGENTS.md` gains exactly one bullet.

## Acceptance criteria

- The three insertions above exist verbatim-in-meaning at the named locations, and the diffs contain nothing else.
- `/validate-skill` passes for both changed skills.
- A spot-check that each skill's existing behavior description (modes, steps, outputs) is byte-identical outside the inserted blocks.

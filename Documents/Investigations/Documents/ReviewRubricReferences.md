# Review Rubric References

Status: exploratory / investigation. It lives in `Documents/Investigations/` because it presents options rather than a decision-complete implementation, so it is never a scheduler input. It becomes a Plan only once the open questions below are answered and it moves to `Documents/Plans/<area>/` with byte-zero `broken-engine-plan/v1` metadata.

## Context

Current Anthropic guidance recommends encoding taste as explicit rubrics — reference documents a verifier agent loads to judge quality against stated criteria, rather than judgment embedded in the prose of whichever skill happens to run.

This repository embeds its taste criteria inside skill bodies. `repo-code-review/SKILL.md` is 278 lines and carries twelve `### ` subsections under `## Correctness Checks`; `code-style-review` owns comment quality; `external-design-interface` owns API shape. A reviewer running one skill cannot cheaply consult another skill's criteria.

## Open questions

1. Which criteria are genuinely reusable across skills? The strongest candidates are comment quality (cited by `code-style-review`, `repo-code-review`, and root `## Directives`) and API/interface shape (`external-design-interface`, `external-architecture-review`). Determinism and allocation rules are already centralized in child `AGENTS.md` files and do not need a rubric.
2. Does extraction reduce or increase total tokens? A rubric that only one skill loads is a net loss — an extra file and an extra read for the same content. Extraction pays only where two or more skills would load the same rubric.
3. Rubric or reference? `.agents/references/` currently holds two files, both effectively extensions of root `AGENTS.md` rather than a shared library. A `references/rubrics/` subtree would be the first genuinely shared reference surface, which is a small architectural decision about that directory's role.

## Known conflicts

- Root `AGENTS.md` `## Directives` KISS/DRY rule: "Extract helpers only for current duplication, never for hypothetical use." A rubric extracted before a second consumer exists violates this directly.
- `/validate-skill` governs skill package structure and bundled-link rules; adding a `references/rubrics/` convention would need to satisfy it.
- Every review already routes through `/codex-review` to Sol. Rubrics change what a reviewer is measured against, so a bad rubric silently degrades every review at once — a wider spread of breakage than the file count suggests.

## Possible approach

Prove duplication first. Identify the specific criteria that appear in two or more skills with meaningfully the same wording, and extract only those. If the honest count is zero, close this document rather than build the subtree.

## Out of scope

Rewriting `repo-code-review/SKILL.md`'s check list, or changing how `/codex-review` dispatches.

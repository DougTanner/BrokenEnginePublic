<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-31T22:14:28.420Z","dependsOn":[]} -->
# Trim Always-Loaded Skill Descriptions

## Context

A skill's frontmatter `description` is always-loaded context: roughly 19.5 KB (~5,000 `bt-token-v1`) of descriptions enters every session before any skill is dispatched. A skill *body* loads only when its skill runs (for example `.agents/skills/compile/SKILL.md` is ~7,556 `bt-token-v1` on every `builder` dispatch), and a skill carrying `disable-model-invocation` (`next-plan`, `next-plan-review`, `save-plan`, `session-audit`, `cleanup-worktrees`, the `gaea2-*` family) is never selected from its description, so only its body size matters. Description length is therefore the only skill text that costs every session.

`.agents/skills/verify-changes/SKILL.md` is the working model at 119 characters: it names the job and the trigger, and nothing else. Twelve model-invocable skills instead restate their own procedure, enumerate their body content, or list domain synonyms in the description. Root `AGENTS.md` routes to skills by name only, so no `AGENTS.md` edit is required.

Estimated saving: ~1,000 always-on `bt-token-v1` per session.

## Design

Rewrite each listed `description` toward the `verify-changes` model — name the job, the trigger condition, and any load-bearing exclusion; never the procedure, the body's section list, or synonyms a model can infer. Trigger precision is preserved: every rewrite must still fire on the same requests and stay silent on the same non-triggers, and every explicitly load-bearing element named below is kept.

| Skill | Current chars | Rewrite instruction |
|---|---|---|
| `external-design-interface` | 762 | Move the verbatim ask-the-user question at `SKILL.md:8-9` and the do-not list at `:11-13` into the body; keep the `/add-collection-member` routing exclusion in compressed form. |
| `scope-review` | 614 | Drop the restated procedure; the trigger is root `AGENTS.md` Change Workflow Step 5, not the description. |
| `glsl-review` | 610 | Drop the "Covers NaN/Inf…" enumeration and the extension list; the frontmatter `paths:` key at `:13` already scopes the files. |
| `implement-plan` | 600 | Drop the second half, which enumerates body content. |
| `verify-external-claims` | 566 | Drop the Vulkan/GLSL/DirectXMath/C++23 domain enumeration. |
| `resolve-findings` | 548 | Reduce to the job plus the delegated-fix trigger. |
| `external-diagnose-bug` | 541 | Drop the generic symptom synonyms "broken, failing, crashing, throwing, hanging"; keep "desyncing, mismatched CRC", which is not inferable. |
| `analyze-diagsession` | 540 | Reduce to ~250 characters, keeping the `.diagsession`/ETL trigger. |
| `adversarial-review` | 509 | Keep the Tier-3 and explicit-request triggers and the findings-only boundary. |
| `repo-code-review` | 503 | Keep the shader-only/non-C++ exclusion and the style-belongs-to-`code-style-review` split. |
| `update-claude-docs` | 494 | Drop the three synonymous trigger phrasings; keep the audit versus audit-and-fix distinction. |
| `agent-harness` | 487 | Drop the mechanism enumeration; keep the RenderDoc/GPU-capture trigger. |

`external-architecture-review` and `external-refactor-clean` are excluded: their descriptions carry load-bearing negative triggers ("do not select for routine code changes", "only when explicitly requested or chained to") that are the only implicit-selection suppressor, because both deliberately omit `disable-model-invocation` so `external-deep-analysis` can chain to them. `Documents/Plans/Agents/ExternalAnalysisFamilyConsolidation.md` owns them.

The frontmatter schema is owned by `.agents/skills/validate-skill/references/frontmatter-schema.md` and is read, not changed.

## Critical files

- `.agents/skills/external-design-interface/SKILL.md` — frontmatter `description`; body gains the relocated question and do-not list.
- `.agents/skills/scope-review/SKILL.md`, `.agents/skills/glsl-review/SKILL.md`, `.agents/skills/implement-plan/SKILL.md`, `.agents/skills/verify-external-claims/SKILL.md`, `.agents/skills/resolve-findings/SKILL.md`, `.agents/skills/external-diagnose-bug/SKILL.md`, `.agents/skills/analyze-diagsession/SKILL.md`, `.agents/skills/adversarial-review/SKILL.md`, `.agents/skills/repo-code-review/SKILL.md`, `.agents/skills/update-claude-docs/SKILL.md`, `.agents/skills/agent-harness/SKILL.md` — frontmatter `description` only.
- `.agents/skills/verify-changes/SKILL.md` — read-only model.
- `.agents/skills/validate-skill/references/frontmatter-schema.md` — read-only contract.

## In scope

- The frontmatter `description` value of each of the twelve skills in the Design table, rewritten per its instruction.
- `.agents/skills/external-design-interface/SKILL.md` body: one new short block holding the relocated ask-the-user question (`SKILL.md:8-9`) and do-not list (`:11-13`), placed at the start of `## Establish the Design Brief`.

## Out of scope

- The `description` of `external-architecture-review` and `external-refactor-clean`.
- The `description` of any skill not listed in the Design table, including `verify-changes`.
- Every other frontmatter key: `name`, `allowed-tools`, `paths`, `disable-model-invocation`, and `agents/openai.yaml`.
- Every skill body except the single relocation block in `external-design-interface`.
- Root `AGENTS.md`, which routes by skill name only.
- Renaming, merging, adding, or deleting any skill.

## Coordination

`Documents/Plans/Agents/ExternalAnalysisFamilyConsolidation.md` reorganizes the `external-design-interface` body and depends on this plan for the relocated description content; it must land after this plan.

## Risk tier and invariants

Tier 2 — scoped tool behavior: skill-selection triggers for the agent toolchain. No engine runtime, determinism/CRC, wire, serialization, save/replay, threading, or build/bootstrap coordination surface, and no C++ change.

Invariants: every rewritten description still fires on the requests its current text fires on and stays silent on the non-triggers it currently excludes; `external-diagnose-bug` still names desync and CRC-mismatch symptoms; `update-claude-docs` still distinguishes audit from audit-and-fix; `agent-harness` still names RenderDoc capture; `external-design-interface` still routes single-member additions to `/add-collection-member`; no skill entry point is renamed; no bundled script behavior changes.

## Acceptance criteria

- Each of the twelve descriptions is shorter than its current length, and the total of the twelve is at least 3,500 characters shorter than today.
- Each rewritten description names the job and its trigger and contains no restated procedure, no body section list, and no domain or symptom enumeration except the four elements named as kept.
- `external-design-interface`'s ask-the-user question and do-not list appear once, in the body, byte-equivalent in meaning to the removed description text.
- `external-architecture-review` and `external-refactor-clean` descriptions are byte-unchanged.
- `/validate-skill` passes on every touched `SKILL.md`.
- No skill directory or `name` value changes, and no bundled script is edited.

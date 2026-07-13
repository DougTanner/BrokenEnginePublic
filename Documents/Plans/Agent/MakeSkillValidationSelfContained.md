# Make Skill Validation Self-Contained

## Context

Step 9 verification of six edited process skills cannot run the prescribed system `skill-creator` validator in the normal clean environment. Every invocation of `C:\Users\dougt\.codex\skills\.system\skill-creator\scripts\quick_validate.py` exits 1 before reading the target skill because line 10 imports `yaml`, while the active Python 3.12 environment has no `yaml` module (`ModuleNotFoundError: No module named 'yaml'`). The validator otherwise uses `yaml.safe_load` to enforce the complete frontmatter schema, so the session's dependency-free delimiter/name/description fallback is narrower and cannot establish official-validator acceptance.

No repository requirement, bootstrap, wrapper, or other `PyYAML` reference currently owns this dependency. Relying on a package installed into one user's global Python environment would make verification depend on undocumented per-session state and would not give Claude Code and Codex CLI the same reproducible path. This follow-up originates from step 9; the user directed that the durable solution be queued rather than implemented in the active skill-documentation change.

## Design

1. Inspect the current system validators available to both clients and the repository's existing script/dependency-bootstrap conventions. Establish which validator contract is authoritative and whether its implementation/location is stable enough for a repository command to invoke directly.
2. At grill, choose the smallest cross-client boundary from three evidence-backed options: make validation dependency-free while preserving the official schema, deterministically bootstrap an isolated pinned YAML dependency, or add a repository-owned wrapper that locates/runs the authoritative validator and reports missing prerequisites explicitly. Do not depend on ambient global packages or silently substitute a weaker check.
3. Provide one documented repository validation command for `.agents/skills/<skill>` that works from a clean supported checkout under both Claude Code and Codex CLI. Route skill-authoring and verification instructions through that command instead of client-specific or home-directory assumptions.
4. Preserve deterministic output and exit status. Distinguish invalid `SKILL.md` content from validator setup failure, and validate all frontmatter properties currently checked by the authoritative validator.

## Critical files

- `C:\Users\dougt\.codex\skills\.system\skill-creator\scripts\quick_validate.py` — current authoritative implementation and its unconditional `yaml` dependency; inspect only unless ownership is explicitly established outside this repository.
- `.agents/skills/external-skill-creator/SKILL.md` and repository-owned validation script/bootstrap location selected at grill — shared skill-authoring entry point and durable implementation boundary.
- `.agents/skills/verify-changes/SKILL.md` and `AGENTS.md` — verification/process instructions that must use the same clean-environment command where skill files are in the change manifest.
- `.agents/skills/external-grill-plan/SKILL.md`, `.agents/skills/finalize-changes/SKILL.md`, `.agents/skills/implement-plan/SKILL.md`, `.agents/skills/next-plan/SKILL.md`, and `.agents/skills/resolve-findings/SKILL.md` — active-session overlap; update only if the chosen validation contract requires local invocation text rather than one centralized process rule.

## Out of scope

- Installing `PyYAML` globally, changing a developer's system Python, or relying on an already-populated virtual environment.
- Weakening or deleting frontmatter schema checks to make current skills pass.
- Changing skill behavior, the C++ Code Change Process, plan queue semantics, or game/runtime code.
- Adding unit tests; use disposable valid/invalid skill fixtures and clean-environment command checks.

## Acceptance criteria

- From a clean supported environment with no ambient `yaml` module, one documented repository command validates each repository skill under both Claude Code and Codex CLI, or performs a deterministic isolated bootstrap before validation.
- The command accepts all six skills that exposed this gap after their content is otherwise valid, and rejects disposable fixtures for malformed YAML, unexpected keys, invalid names, missing required fields, angle brackets, and overlong descriptions with deterministic nonzero results.
- Setup/dependency failure is reported distinctly from invalid skill content; no success path falls back silently to delimiter/name/description-only validation.
- Documentation identifies the dependency/version and ownership boundary, or records why the dependency-free implementation is contract-equivalent to the authoritative system validator.

## Notes

- Decision plan: present the three boundary options with repository and actual-validator evidence at grill; do not preselect one from this residual alone.
- Developer workflow only. No C++ build, runtime, determinism/CRC, replay, wire protocol, `kiVersion`/`.pack`, shader, client/server guard, allocation-tracked, or agent-harness exposure.
- This plan overlaps the active session's `AGENTS.md` and six process skills. Reconcile those files against the final landed session before execution; ordinary overlap is not a dependency.

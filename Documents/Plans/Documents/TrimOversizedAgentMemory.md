# Trim Root Agent Memory

## Context

Root [`AGENTS.md`](../../../AGENTS.md) is 5,464 `bt-token-v1`, above the 4,000-token budget for a repository-wide hub. Because every scoped task loads it, duplicated explanation and avoidable narration have a repository-wide context cost. The document must remain the authoritative entry point for repository-wide process, risk gates, delegation, core invariants, and links to narrower ownership.

## Design

- Apply the `update-claude-docs` removal test: retain a root instruction only when omitting it could cause a future agent to make a worse repository-wide decision.
- Condense repeated workflow explanation, table prose, emphasis, and detail already owned by linked skills or child `AGENTS.md` documents. Prefer short rules and ownership links over implementation narration.
- Preserve the operational meaning of the delegation model, risk tiers, Change Workflow and final-evidence triggers, ambiguity and diagnosis rules, repository topology, and cross-cutting engine invariants.
- Keep current-state guidance only. Remove temporary notes and historical framing; do not replace deleted text with new policy or architecture detail.
- Measure with [`Measure-Tokens.ps1`](../../../.agents/scripts/Measure-Tokens.ps1) until the root document is at most 4,000 `bt-token-v1`.

## Critical files

- [`AGENTS.md`](../../../AGENTS.md) — sole implementation target and repository-wide instruction hub.

## Out of scope

- Editing child `AGENTS.md` documents, `CLAUDE.md` import stubs, skills, references, code, build configuration, or queue files.
- Changing workflow authority, risk classification, determinism/CRC rules, client/server boundaries, or other repository policy.
- Adding new documentation, compatibility wording, changelog entries, or implementation inventories.

## Acceptance criteria

- `Measure-Tokens.ps1` reports root `AGENTS.md` at or below 4,000 `bt-token-v1`.
- A fresh coherence review against the pre-edit root confirms every repository-wide decision rule and workflow gate retains its operational meaning, with narrower detail delegated only to an existing valid link.
- Every retained relative Markdown link resolves, repository directory signposts remain accurate, and no temporary or historical prose remains.
- `git diff --check` passes and implementation changes only root `AGENTS.md`.

## Notes

This is docs-only Tier 1 work. Queue completion and landing use the final-evidence path. No build, unit test, or runtime harness run is required.

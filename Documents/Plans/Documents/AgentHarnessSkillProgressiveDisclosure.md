<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Split the Agent-Harness Skill's Command Reference

## Context

`.agents/skills/agent-harness/SKILL.md` measures **12,060 `bt-token-v1`** (368 lines, 48,240 bytes) by `.agents/scripts/Measure-Tokens.ps1`. The `/validate-skill` contract says to consider progressive disclosure above 10,000 `bt-token-v1` and to target at most 15,000, so the file is over the disclosure threshold but under the ceiling — a standing context cost on every session that touches the harness, not a validation failure. Pre-existing; the 2026-07-21 session added roughly 180 bytes.

The natural extraction candidate is the `## Command reference` block, lines 164-306 (`### Shared` / `### Server-only` / `### Client-only`), measured at **5,749 `bt-token-v1`**. It is already fenced between horizontal rules and is lookup material — an agent needs it when composing a specific command, not when deciding whether to use the harness. Moving it leaves roughly 6,300 `bt-token-v1` in `SKILL.md`: launch, claim, envelope, canonical workflow, verification reporting, and caveats. The skill directory already has a `scripts/` subdirectory but no `references/`.

## Design

- Move `## Command reference` (`SKILL.md:164-306`) verbatim into a new `references/` file under `.agents/skills/agent-harness/`, preserving every command name, schema, and example exactly — this is a relocation, not a rewrite.
- Because the extracted file exceeds 2,000 `bt-token-v1`, give it a table of contents per the `/validate-skill` rule.
- Leave a short pointer in `SKILL.md` where the block was, naming the reference and when to read it, so an agent composing a command still finds it in one hop.
- Re-check every intra-document reference to the moved commands from the remaining `SKILL.md` sections (canonical workflow, verification report, caveats) and from any other skill or AGENTS.md that links into the command reference; fix links that would break.
- Do not change command behavior, schemas, names, or the harness itself.

## Critical files

- `.agents/skills/agent-harness/SKILL.md` — source of the extraction and the surviving hub.
- `.agents/skills/agent-harness/references/` — new directory for the extracted reference.
- Any document linking into the command reference (search for `agent-harness/SKILL.md` and for individual command names before moving).

## Out of scope

- Rewriting, condensing, or correcting the command documentation — relocation only; content edits are a separate concern.
- Other oversized skills or AGENTS.md documents; root `AGENTS.md` is owned by `Documents/Plans/Documents/TrimOversizedAgentMemory.md`.
- The harness executable, command schemas, or `Tools/AgentHarness/`.
- Adding new commands or capabilities.

## Acceptance criteria

- `Measure-Tokens.ps1` reports `.agents/skills/agent-harness/SKILL.md` below 10,000 `bt-token-v1`.
- The extracted reference carries a table of contents and every command that was in `SKILL.md:164-306`, with no command lost or altered.
- `/validate-skill` passes on the changed `SKILL.md`.
- Every relative link into and out of the moved block resolves.

## Notes

- Documentation-only Tier 1 work: no build, no code, no runtime harness run required. `git diff --check` passes.
- No determinism/CRC, wire, `.pack`, or client/server exposure.

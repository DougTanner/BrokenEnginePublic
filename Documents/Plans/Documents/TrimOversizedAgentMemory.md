# Trim Oversized Agent Memory Documents

## Context

The AGENTS.md size contract targets at most 4,000 bt-token-v1 for the root and cross-cutting `Common` hubs and 2,000 for leaf documents. Current measurements show nine pre-existing overruns intentionally out of scope: `AGENTS.md` (5,464), `Common/AGENTS.md` (4,548), `DataPacker/Source/ExportJobs/AGENTS.md` (3,288), `Documents/Plans/AGENTS.md` (2,619), `Engine/Data/Shaders/Water/AGENTS.md` (3,714), `Engine/Source/Frame/AGENTS.md` (2,931), `Engine/Source/Graphics/AGENTS.md` (3,972), `Engine/Source/Network/AGENTS.md` (2,092), and `Engine/Source/Network/Server/AGENTS.md` (2,124). Together they exceed their individual targets by 8,752 bt-token-v1, increasing every affected root-to-leaf context chain.

These files share one root cause and implementation boundary: durable agent-memory guidance has accumulated duplicated parent/sibling rules, implementation narration, and detail better expressed by existing code or architecture references. They should be condensed together under the same documentation rubric and token measurement rather than queued as unrelated subsystem work.

## Design

- Apply the `update-claude-docs` removal test to each affected document: retain only guidance whose absence would cause a future agent to make a worse decision.
- Remove duplicated parent/sibling material, member or file inventories, method narration, and repeated emphasis. Keep non-obvious invariants, executable workflows, trust-boundary rules, and subsystem ownership facts.
- Preserve established vocabulary, present-tense statements, and useful cross-references. Do not replace removed prose with new implementation detail or duplicate architecture documents.
- Measure each file after editing and continue trimming until `AGENTS.md` and `Common/AGENTS.md` are each at most 4,000 bt-token-v1 and every affected leaf is at most 2,000 bt-token-v1.

## Critical files

- Root and shared hubs: `AGENTS.md`, `Common/AGENTS.md`.
- Data, planning, and graphics leaves: `DataPacker/Source/ExportJobs/AGENTS.md`, `Documents/Plans/AGENTS.md`, `Engine/Data/Shaders/Water/AGENTS.md`, `Engine/Source/Graphics/AGENTS.md`.
- Frame and network leaves: `Engine/Source/Frame/AGENTS.md`, `Engine/Source/Network/AGENTS.md`, `Engine/Source/Network/Server/AGENTS.md`.

## Out of scope

- Changing runtime code, public interfaces, build configuration, or engine behavior.
- Rewriting AGENTS.md files that already meet their individual size target.
- Changing the one-line `CLAUDE.md` import stubs or performing another link-markup cleanup.
- Adding new policies, invariants, architecture documents, or historical/changelog narration.

## Acceptance criteria

- `Measure-Tokens.ps1` reports `AGENTS.md` and `Common/AGENTS.md` at or below 4,000 bt-token-v1 and each of the seven affected leaf documents at or below 2,000.
- A direct coherence review confirms every retained instruction is current, non-duplicative across its effective root-to-leaf chain, and preserves the original operational meaning.
- Every retained relative Markdown link resolves, `git diff --check` passes, and no files outside the nine named AGENTS.md documents change during implementation.

## Notes

This is docs-only Tier 1 implementation work, but its queue completion and landing use the final-evidence path. It has no determinism/CRC, `kiVersion`/`.pack`, replay, wire protocol, client/server guard, allocation-tracked runtime, shader-code, build, or live-harness exposure. Do not add unit tests or run client/server builds for this documentation-only change.

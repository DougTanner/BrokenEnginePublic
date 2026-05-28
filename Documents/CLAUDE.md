# Documents - Design-Time Reference and Planning

Holds design-time documentation and two parallel planning trees. No build artifacts, no code.

## Reference Material

- `C++StyleGuide.txt` — the 60 numbered style rules (Hungarian notation, Allman braces, `auto` restrictions, DirectXMath conventions). Source of truth for the `code-style-review` skill.
- `FloatingPointDeterminism.txt` — the rollback-and-replay determinism contract: `/fp:strict`, FMA3 disabled, per-thread MXCSR, fixed 32 Hz timestep, deterministic RNG, dual CRC. Read before touching simulation math.
- `azure-game-server-guide.docx` — server-hosting deployment notes.
- `Architecture/` — Mermaid diagrams (`FrameUpdatePipeline.md`, `GameReconciliation.md`, `Network.md`). The `update-architecture-diagrams` skill keeps these current; CLAUDE.md docs link to them rather than duplicating their content.

## Planning Trees

Plan files (the units `/next-plan` executes) live in two sibling directories, split by whether the work adds a capability:

| Directory | Scope |
|-----------|-------|
| [`Plans/`](Plans/CLAUDE.md) | **Refactors and bugfixes.** Debt reduction — cleaning, decomposing, renaming, deleting dead code, fixing races/NaNs/precision, defensive shader clamps. Changes *how* the engine is built or how correctly it runs, not what it does. |
| [`Features/`](Features/CLAUDE.md) | **Brand-new additions.** New render passes, effects, systems, collections, network/audio capabilities, dev tooling that ships in the binary. Adds a capability the engine did not have. |

Deciding test: *does this plan give the engine a capability it didn't have before?* Yes → `Features/`, No → `Plans/`. Edge cases (same-capability library swaps, dev tooling, precision refactors that merely unblock larger maps) stay in `Plans/` unless their *point* is the new capability. Each directory organizes its files into area subfolders (`Engine/`, `Frame/`, `Graphics/`, etc.) and owns its own `Order.md` index plus authoring/row-format rules — see the two child CLAUDE.md files.

## Scoring Anchors (canonical — both `Order.md` files reference here)

Each `Order.md` row carries `Score = Effort − Impact + Risks`; lower = higher priority. Scores are not comparable across the two files (originally one sort, later split). Calibrate against neighbouring rows; do not default to the middle.

**Effort** (size of change):

| Score | Anchor |
|-------|--------|
| 1 | Quick Win — single-file mechanical change, < half day, no design decisions |
| 2 | Small — one subsystem, half- to full-day, narrow scope |
| 3 | Medium — multi-day, several files, some design choices |
| 4 | Large — multi-day with research / coordination |
| 5 | Architectural — week+, spans sessions, may need subplans |

**Impact** (value if executed):

| Score | Anchor |
|-------|--------|
| 1 | Cosmetic — terminology, IDE-view only, comment cleanup, single-digit line removal |
| 2 | Modest — minor consistency/perf, eliminates noise, narrow dev-experience win |
| 3 | Real — visible bug fix, meaningful perf/correctness gain, removes hundreds of lines |
| 4 | Significant — fixes a determinism/desync source, eliminates a real bug class, major code-quality lift |
| 5 | High — fixes a critical bug, unlocks a major scenario, enables further work |

**Risks** (chance / blast radius of breakage):

| Score | Anchor |
|-------|--------|
| 0 | None — pure docs, dead code, comment-only, IDE-view only |
| 1 | Low — mechanical refactor with compile-checked invariants, narrow, easily reverted |
| 2 | Moderate — touches gameplay/runtime, needs playtest, has fallbacks |
| 3 | High — affects determinism / CRC / network protocol / cross-frame state, hard to verify |
| 4 | Architectural — broad impact, hard to revert, may interact with in-flight work |

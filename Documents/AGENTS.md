# Documents - Design-Time Reference and Planning

Holds design-time documentation and two parallel planning trees. No build artifacts, no code.

## Reference Material

- `FreshMachineSetup.md` — ordered fresh-machine bootstrap: Developer Mode/symlink privilege, clone, one-time primary ThirdParty build, primary data export, first wrapper run with ledger init (session start rebuilds the shared AgentTools and ThirdParty binaries incrementally and prebuilds DataPacker Release for worktree seeding). Human-facing counterpart to the wrapper scripts under `.agents/scripts/`.
- `C++StyleGuide.txt` — numbered style rules (Hungarian notation, Allman braces, `auto` restrictions, DirectXMath conventions). Source of truth for the `code-style-review` skill.
- `FloatingPointDeterminism.txt` — the rollback-and-replay determinism contract: `/fp:strict`, FMA3 disabled, per-thread MXCSR, fixed 32 Hz timestep, deterministic RNG, single shared CRC. Read before touching simulation math.
- `UserInterfaceDesign.txt` — the ImGui layout contract for player-facing screens: one scale factor (`engine::UiScale()`), the three sizing rules, shared layout constants, pivot centering, size-to-content, themed buttons, and vertical rhythm. Layout counterpart to `FloatingPointDeterminism.txt`; source of truth for `Ui/Screens/` geometry. Read before touching menu/HUD layout.
- `azure-game-server-guide.md` — Azure VM hosting guide for the dedicated server: sizing/costs, NSG rules, deployment set, remote debugging.
- `Architecture/` — external detail too large for AGENTS.md: Mermaid diagrams (`FrameUpdatePipeline.md`, `GameReconciliation.md`) and the network protocol specification (`Network.md`). Relevant subsystem AGENTS.md files link to them; update the affected document when its phase ordering, reconciliation flow, or protocol contract changes.

## Planning Trees

Plan documents live in two sibling directories, split by whether the work adds a capability:

| Directory | Scope |
|-----------|-------|
| [`Plans/`](Plans/AGENTS.md) | **Refactors and bugfixes.** Debt reduction — cleaning, decomposing, renaming, deleting dead code, fixing races/NaNs/precision, defensive shader clamps. Changes *how* the engine is built or how correctly it runs, not what it does. |
| [`Features/`](Features/AGENTS.md) | **Brand-new additions.** Manually executed; never scheduler-tracked. |

Deciding test: *does this plan give the engine a capability it didn't have before?* Yes → `Features/`, No → `Plans/`. Executable Plans carry a byte-zero, Git-tracked metadata marker. WorktreeCli selects them deterministically by immutable creation time and canonical path. Files without the marker and all Features files are manual. Each tree uses area subfolders (`Engine/`, `Frame/`, `Graphics/`, etc.).

## Historical scoring anchors

Existing estimates remain useful human context but never schedule work. Executable Plans use immutable metadata creation time and canonical path. Size-removal anchors use the deterministic `bt-token-v1` estimate from [Measure-Tokens.ps1](../.agents/scripts/Measure-Tokens.ps1), not exact model tokens.

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
| 1 | Cosmetic — terminology, IDE-view only, comment cleanup, under ~100 `bt-token-v1` removed |
| 2 | Modest — minor consistency/perf, eliminates noise, narrow dev-experience win |
| 3 | Real — visible bug fix, meaningful perf/correctness gain, removes at least ~1,000 `bt-token-v1` |
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

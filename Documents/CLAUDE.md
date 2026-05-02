# Documents

Design-time documentation: style guide, architecture diagrams, floating-point notes, and two parallel planning trees.

## Plans vs Features

Planning work is split into two categories, each with its own directory and its own `Order.md` index.

| Directory | Scope | Examples |
|-----------|-------|----------|
| `Plans/` | **Refactors and bugfixes.** Debt reduction: cleaning, decomposing, renaming, deleting dead code, fixing races, NaN guards, precision issues, oversized files, unused includes, shader defensive clamps. Does not change what the engine *does* — only how it is built or how correctly it runs. | `Refactor_*`, `Architecture_*`, `ShaderReview/*` (defensive fixes), `FrameRelativePositions` (fixes float precision), `ReverseZDepth` (fixes z-fighting), `ReplaceDirectXTKAudioWithMiniaudio` — wait, this one is a feature (see below) |
| `Features/` | **Brand-new additions.** Product direction: new render passes, new effects, new systems, new collections, new network capabilities, new audio systems, new dev tooling that ships in the binary. Adds a capability the engine did not previously have. | `SkyboxRenderPass`, `HdrResolveAndColorGrading`, `ocean-phase-*`, `DecalSystem`, `AdaptiveMusic`, `AddTracyProfiler`, `SpirvOptIntegration`, `ReplaceDirectXTKAudioWithMiniaudio` (enables cross-platform), `Future_*` |

### Deciding where a new plan goes

Ask: *does this plan give the engine a capability it didn't have before?*

- **Yes** → `Features/`. Examples: a new shader term, a new collection, a new network layer, a new profiler integration, a new build-pipeline tool, a library replacement whose point is to enable a new platform.
- **No** → `Plans/`. Examples: cleaning up existing code, fixing a bug, decomposing a large file, deleting dead includes, adding defensive clamps to an existing shader, swapping an `unordered_map` for `flat_map` in an existing hot path, adding clang-format configs (dev-time tooling that doesn't ship).

Edge cases:
- **Same-capability library swap**: usually `Plans/`. But if the *point* of the swap is a new capability (cross-platform, new codec, new latency tier), it's `Features/`.
- **Dev tooling**: `Plans/` if it's pure dev-time (clang-format, clang-tidy configs). `Features/` if it ships in the binary (Tracy integration) or changes build artifacts (spirv-opt optimizes shipped SPIR-V).
- **Precision/correctness refactors that enable a new scenario**: stay in `Plans/`. `FrameRelativePositions` fixes float jitter at distance — it enables larger maps but doesn't add a feature, it unbreaks one.

## Scoring (applies to both directories)

Each row in either `Order.md` has `Score = Effort − Impact + Risks`. Lower score = higher priority. Scores are not comparable across the two directories — they were originally derived from a single sort and then split.

### Anchors

Calibrate against neighbouring rows in the relevant `Order.md` before assigning. Pick the anchor whose description best matches; do not default to the middle.

**Effort** (size of the change):

| Score | Anchor | Examples |
|-------|--------|----------|
| 1 | Quick Win — single-file mechanical change, < half day, no design decisions | grep-and-rename, dead-code delete, comment fix, single-call-site swap, single-flag rename |
| 2 | Small — one subsystem, half- to full-day, narrow scope | one function refactor, one helper extraction, single-collection field rename |
| 3 | Medium — multi-day, touches several files, some design choices | decomposing a 300-line function, introducing a new helper used in ~10 sites, descriptor-set audit across 3 shaders |
| 4 | Large — multi-day with research / coordination needed | architectural refactor, multi-file split, cross-subsystem audit |
| 5 | Architectural — week+ effort, spans multiple sessions, may need its own subplans | major rework like FrameRelativePositions, deep network protocol changes, file-format migrations |

**Impact** (value if executed):

| Score | Anchor | Examples |
|-------|--------|----------|
| 1 | Cosmetic — terminology, IDE-view only, comment cleanup, removes single-digit lines | wrong include path in `.vcxproj.filters`, dead `[[maybe_unused]]`, doc rephrase |
| 2 | Modest — minor consistency or perf, eliminates noise, narrow developer-experience win | flags-pack-into-byte savings, one defensive `normalize` guard, one workbuffer migration |
| 3 | Real — visible bug fix, meaningful perf/correctness gain, removes hundreds of lines | guard consolidation across 5 sites, decomposing a 200-line function, fixing one shader-NaN class |
| 4 | Significant — fixes a determinism/desync source, eliminates a real bug class, major code-quality lift | layer-violation cleanup, hot-path allocation removal, descriptor-set unification across 3 subsystems |
| 5 | High — fixes a critical bug, unlocks a major scenario, enables further work | reconciliation invariant fix, frame-relative positions for distance precision, transfer-barrier bugfix |

**Risks** (chance / blast radius of breakage):

| Score | Anchor | Examples |
|-------|--------|----------|
| 0 | None — pure docs, dead code, comment-only, IDE-view only | renaming inside `.vcxproj.filters`, deleting an unused forward decl, fixing a typo |
| 1 | Low — mechanical refactor with compile-checked invariants, narrow scope, easily reverted | single-file dead-code delete, single-callsite signature change, RAII wrapper introduction |
| 2 | Moderate — touches gameplay or runtime code, needs playtest, has fallbacks | combat-logic tweak, descriptor-set rebind, one-shader perf rework |
| 3 | High — affects determinism / CRC / network protocol / cross-frame state, hard to fully verify | reconciliation rework, fleet-state ordering, cross-grid transfer changes |
| 4 | Architectural — broad impact, hard to revert, may interact with other in-flight work | major file split, layer redesign, save-format change |

## Rules (also stated in each subdirectory's `CLAUDE.md`)

- Any new plan must be added to the appropriate `Order.md` under the right section.
- When a plan is executed, remove it from `Order.md` and delete the plan file from disk.
- When a plan changes category during its lifetime (rare — e.g., scope creep turns a refactor into a new system), move the file and update both `Order.md` files.

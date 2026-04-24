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

## Rules (also stated in each subdirectory's `CLAUDE.md`)

- Any new plan must be added to the appropriate `Order.md` under the right section.
- When a plan is executed, remove it from `Order.md` and delete the plan file from disk.
- When a plan changes category during its lifetime (rare — e.g., scope creep turns a refactor into a new system), move the file and update both `Order.md` files.

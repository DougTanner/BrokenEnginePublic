---
name: external-architecture-review
description: Performs a multi-perspective architectural review of a codebase area, focusing on *shape* concerns — dependency structure, module depth (Ousterhout), coupling/cohesion, determinism, frame-phase and thread-model alignment, shader-CPU consistency, ThirdParty library-replacement opportunities, and the structural anti-patterns characteristic of iteratively AI-generated code (dead modules, cosmetic/broken abstractions, pattern abandonment, cross-file duplication, inter-module seams). Line-level concerns (complexity, hot-path allocations, bool→Flags, file size) belong to `/external-refactor-clean`, which this skill hands off to. Only invoke when the user explicitly requests it (e.g., "/external-architecture-review", "run an architecture review") or when another skill explicitly instructs it. Never trigger autonomously from general code questions or during routine code changes.
disable-model-invocation: true
allowed-tools: [Read, Grep, Glob, Agent]
---

# Architecture Review

Performs an architectural review of a specified codebase area using parallel analysis agents. Designed for a C++23 data-oriented Vulkan game engine with Common → Engine → Projects layering. Applies Ousterhout's deep-modules lens throughout: small interface hiding significant complexity is good; shallow modules are a smell. Because this codebase was built through iterative AI-generation sessions, the review also hunts the structural residue that process characteristically leaves — dead modules, cosmetic abstractions, abandoned patterns, and cross-session integration seams. Security auditing is deliberately out of scope: findings are structural only; injection, auth, secrets, crypto, and CORS belong to a dedicated security pass, not this skill.

**Scope boundary:** This skill owns shape. It does NOT flag function length, nesting depth, hot-path heap allocations, bool-proliferation, or `#ifdef` width — those are line-level concerns owned by `/external-refactor-clean`. If the review surfaces line-level issues during exploration, note them briefly and recommend running `/external-refactor-clean` for depth.

## Arguments

The user provides a target path (file or directory) to review. If no path is given, ask for one.

**Recursion**: By default, only analyze files directly in the specified directory (non-recursive) — subagents use non-recursive glob patterns (e.g., `path/*.h`). Recurse into sub-directories only when the caller explicitly requests it ("recursive", "include subdirectories") — then subagents use recursive patterns (`path/**/*.h`). Pass the active mode to every subagent.

## Instructions

### 1. Launch Parallel Analysis Agents

Use the Agent tool to launch five subagents in parallel, all with `model: "fable"`. Agent A is locate-shaped — use `subagent_type: "Explore"` (if unavailable, fall back to `general-purpose` with the explorer role stated at the top of the prompt). Agents B–E judge as well as locate — use `subagent_type: "general-purpose"` with the reviewer role stated at the top of the prompt. Each agent receives the target path (and the recursion mode) and produces a focused report. Each owns a short, related checklist — keep the groupings as defined below; do not merge them back into one agent.

#### Agent A: Dependency Structure & Layering (subagent_type: Explore)

Prompt the agent to:
- Map `#include` dependencies for all `.h` and `.cpp` files in the target area
- Identify headers included but not used (no symbols referenced) — include the count and file list in this review's report. Macro, template, and transitive usage make this false-positive-prone: tag each entry HIGH / MEDIUM / LOW confidence, and spot-verify before tagging HIGH
- Identify orphan / dead modules — files whose exported symbols have zero callers in production code; cross-reference test-only usage so a module that is tested but never called in the shipping build isn't masked as live. Report these as dead-code candidates.
- Flag transitive includes that should be made direct
- Detect circular or near-circular include chains
- Report include-graph shape: which headers are hubs (pulled in by many files), which files pull in the largest transitive closures?
- **Layer integrity** — `Projects/` reaching into `Engine/` internals; `Common/` depending on `Engine/` or `Projects/`; `Engine/` *types* naming game concepts (e.g. an `engine::` enumerator only the game uses). Engine code *reading* `game::` types, globals, or compile-time symbols is by design, not a violation (root `CLAUDE.md` §Key Patterns)

#### Agent B: Simulation & Threading Invariants (subagent_type: general-purpose)

Prompt the agent to evaluate:
- **Determinism** — RNG ordering, floating-point reorder across threads that could affect replay CRC, read/write ordering between Interpolate and PostRender phases, cross-build parity of `SharedMembers()`
- **Thread model** — `gpMultithreading->Dispatch()` data-access patterns; shared mutable state in parallel regions; `PersistentWorker` usage
- **Frame-phase alignment** — are systems operating in the correct phase (Update vs PostRender vs Interpolate)? Any phase-boundary violations?

#### Agent C: Client/Server & Data Shape (subagent_type: general-purpose)

Prompt the agent to evaluate:
- **Client/Server separation** — `BT_CLIENT`/`BT_SERVER` paths that should differ but don't; client-only code not isolated from server build
- **Collection shape** — `SharedMembers()`/`ClientMembers()`/`Members()` pattern compliance; members stored in the wrong phase (interpolated vs post-render); collections whose member list has grown so large the struct has lost cohesion
- **Shader/CPU consistency** — shader constants or layouts that have diverged from their C++ counterparts; magic numbers in shaders that should reference shared definitions (only when the target area contains shaders or shader-facing CPU code — skip otherwise)

#### Agent D: Cohesion & AI-Generation Anti-Patterns (subagent_type: general-purpose)

Prompt the agent to evaluate:
- **Manager patterns** — `gp*` singletons, unnecessary cross-manager references, god-managers
- **Cohesion (Ousterhout deep modules)** — where does understanding one concept require bouncing between many small files? Where are modules so shallow that the interface is nearly as complex as the implementation? Where do tightly-coupled modules create integration risk in the seams?
- **AI-generation structural anti-patterns** — this codebase is iteratively AI-generated; hunt the residue (structural only, no security):
  - *Cosmetic & broken abstractions* — interfaces / abstract bases with a single implementation that add no isolation (deleting them and using the concrete type changes no behavior); a defined interface bypassed by referencing concrete types directly elsewhere; abstractions that relocate complexity rather than hide it, forcing callers to understand internals (leaky). Apply the deep-modules lens already central to this skill.
  - *Pattern abandonment* — an established engine pattern (`SharedMembers()`/`ClientMembers()`, manager-singleton shape, Collection registration / `ForEach` helpers) followed in early modules but dropped in later-added ones; sibling files whose conventions diverge, signalling a later-generated block that lost the original context.
  - *Cross-file duplication* — near-duplicate functions or logic blocks (~10+ lines) recurring across files from context loss during generation; flag because one copy can drift or receive a fix the other misses. (In-function duplication stays with `/external-refactor-clean`.)
  - *Inter-module contract seams* — integration edges where two modules show divergent naming, error-handling, or abstraction styles (a sign they were generated in different sessions); verify the producing side's output assumptions match the consuming side's — the highest-probability spot for silent contract violations.

#### Agent E: ThirdParty Library Replacement Opportunities (subagent_type: general-purpose)

Goal: identify cohesive in-house code that could be deleted in favor of a permissively-licensed library dropped into `/ThirdParty/` — benefits are codebase shrinkage and access to a battle-tested implementation.

Instruct the agent to first read `ThirdParty/CLAUDE.md` for the **License Policy** and list current `/ThirdParty/` subdirectories (skip suggesting anything already imported; do suggest extending coverage of an already-imported library when the in-house code overlaps it).

Prompt the agent to:
- Find cohesive code clusters (single file, file group, or small subsystem) that implement a well-known reusable problem with no engine-specific reason to be in-house. Typical candidates: data structures, parsers/serializers, compression, math primitives, container utilities, string/path helpers, hashing/CRC, file-format readers, image/audio decoding, geometry/mesh utilities.
- Skip code that is engine-specific by design (frame pipeline, collections, manager singletons, gameplay logic, Vulkan/shader integration glue).
- For each candidate, propose a **specific** replacement library and verify its license against the **License Policy** in `ThirdParty/CLAUDE.md` (read above) — that file is the sole authority; do not filter against a remembered license list. Reject anything not on its allow list.
- Prefer libraries that are widely adopted in the C++ game-engine / graphics / systems space and actively maintained.
- Be conservative: do NOT propose libraries that would require heavy build-system changes, drag in large transitive dependencies, or replace ≲50 lines of trivial code.

Report per candidate:
- **Module / cluster**: path(s) and approximate line range
- **Lines removable**: rough LOC that would be deleted
- **Proposed library**: name, license, one-line justification (battle-tested signal: adoption / maintenance status)
- **Risks**: API mismatch, performance characteristics vs in-house, integration cost, transitive deps
- **Confidence**: HIGH / MEDIUM / LOW

### 2. Consolidate Results

After all agents complete, read their reports and merge into a single architecture review. Deduplicate findings that appear in multiple agent reports. Cross-reference findings to identify systemic issues (e.g., a layer violation that also causes include-chain bloat).

### 3. Synthesize Recommendation

After deduplicating findings, provide an opinionated recommendation: what is the single most impactful architectural improvement? Be specific — name the modules, the proposed change, and why it matters most. The user wants a strong read, not just a list.

### 4. Output Consolidated Report

```
## Architecture Review: [target path]

### Overview
[3-5 sentence summary of architectural health, key strengths, and primary concerns]

### Dependency Structure & Layering
[Consolidated findings from Agent A]
- Include-graph shape (hubs, worst transitive closures)
- Circular or near-circular chains
- Unused includes (with confidence tags)
- Orphan / dead modules (zero-caller files)
- Layer integrity

### Simulation & Threading Invariants
[Consolidated findings from Agent B]
- Determinism
- Thread model
- Frame-phase alignment

### Client/Server & Data Shape
[Consolidated findings from Agent C]
- Client/Server separation
- Collection shape (SharedMembers/ClientMembers, phase placement, cohesion)
- Shader/CPU consistency

### Cohesion & AI-Generation Anti-Patterns
[Consolidated findings from Agent D]
- Manager patterns
- Deep-module cohesion notes
- AI-generation structural anti-patterns (dead modules, cosmetic/broken abstractions, pattern abandonment, cross-file duplication, inter-module seams)

### ThirdParty Library Replacement Opportunities
[Consolidated findings from Agent E]
- Per candidate: module/cluster, lines removable, proposed library (name + license), risks, confidence
- License-rejected candidates (note any tempting libraries excluded for copyleft)

### Cross-Cutting Concerns
[Systemic issues that span multiple categories]

### Handoff to `/external-refactor-clean`
[One-line notes on line-level issues surfaced in passing — do NOT enumerate; that skill re-scans with the right depth]

### Prioritized Recommendations
1. [Highest priority] — [justification] — [effort estimate]
2. ...
3. ...

### Architecture Health: [HEALTHY / MINOR CONCERNS / NEEDS ATTENTION / CRITICAL]
[Brief justification]
```

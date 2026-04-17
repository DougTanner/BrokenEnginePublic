---
name: external-architecture-review
description: Performs a multi-perspective architectural review of a codebase area, focusing on *shape* concerns — dependency structure, deep-modules (Ousterhout), coupling/cohesion, determinism, frame-phase and thread-model alignment, shader-CPU consistency. Line-level concerns (complexity, hot-path allocations, bool→Flags, file size) belong to `/external-refactor-clean`, which this skill hands off to. Only invoke when the user explicitly requests it (e.g., "/external-architecture-review", "run an architecture review") or when another skill explicitly instructs it. Never trigger autonomously from general code questions or during routine code changes.
disable-model-invocation: true
allowed-tools: [Read, Grep, Glob, Agent]
---

# Architecture Review

Performs an architectural review of a specified codebase area using parallel analysis agents. Designed for a C++23 data-oriented Vulkan game engine with Common → Engine → Projects layering. Applies the "deep modules" lens (John Ousterhout): a well-designed module has a small interface hiding significant complexity. Shallow modules (large interface, thin implementation) are a code smell.

**Scope boundary:** This skill owns shape. It does NOT flag function length, nesting depth, hot-path heap allocations, bool-proliferation, or `#ifdef` width — those are line-level concerns owned by `/external-refactor-clean`. If the review surfaces line-level issues during exploration, note them briefly and recommend running `/external-refactor-clean` for depth.

## Arguments

The user provides a target path (file or directory) to review. If no path is given, ask for one.

**Recursion**: By default, recurse into sub-directories. If the caller specifies "non-recursive" or "only files directly in this directory", pass this constraint to each subagent so they use non-recursive glob patterns (e.g., `path/*.h` instead of `path/**/*.h`).

## Instructions

### 1. Launch Parallel Analysis Agents

Use the Agent tool to launch two subagents in parallel (`subagent_type: "Explore"`, `model: "opus"`). Each agent receives the target path (and any recursion constraint) and produces a focused report.

#### Agent A: Dependency Structure (subagent_type: Explore)

Prompt the agent to:
- Map `#include` dependencies for all `.h` and `.cpp` files in the target area
- Identify headers included but not used (no symbols referenced) — note count; details go to `/external-refactor-clean`
- Flag transitive includes that should be made direct
- Detect circular or near-circular include chains
- Check that `Common/ExternalHeaders.h` is used for standard library headers (not individual files)
- Report include-graph shape: which headers are hubs (pulled in by many files), which files pull in the largest transitive closures?
- Classify dependencies into categories (Ports & Adapters):
  - In-process: pure computation, in-memory state, no I/O
  - Local-substitutable: dependencies with local stand-ins
  - Remote-but-owned: own services across boundaries
  - True external: third-party libraries/services (mock boundary)

#### Agent B: Patterns, Cohesion & Engine Invariants (subagent_type: Explore)

Prompt the agent to evaluate:
- **Layer integrity** — `Projects/` reaching into `Engine/` internals; `Engine/` depending on game-specific types except via `game::gpGame`; `Common/` depending on `Engine/` or `Projects/`
- **Manager patterns** — `gp*` singletons, unnecessary cross-manager references, god-managers
- **Collection shape** — `SharedMembers()`/`ClientMembers()`/`Members()` pattern compliance; members stored in the wrong phase (interpolated vs post-render); collections whose member list has grown so large the struct has lost cohesion
- **Client/Server separation** — `BT_CLIENT`/`BT_SERVER` paths that should differ but don't; client-only code not isolated from server build
- **Determinism** — RNG ordering, floating-point reorder across threads that could affect replay CRC, read/write ordering between Interpolate and PostRender phases, cross-build parity of `SharedMembers()`
- **Thread model** — `gpMultithreading->Dispatch()` data-access patterns; shared mutable state in parallel regions; `PersistentWorker` usage
- **Frame-phase alignment** — are systems operating in the correct phase (Update vs PostRender vs Interpolate)? Any phase-boundary violations?
- **Shader/CPU consistency** — shader constants or layouts that have diverged from their C++ counterparts; magic numbers in shaders that should reference shared definitions
- **Cohesion (Ousterhout deep modules)** — where does understanding one concept require bouncing between many small files? Where are modules so shallow that the interface is nearly as complex as the implementation? Where do tightly-coupled modules create integration risk in the seams?

### 2. Consolidate Results

After all agents complete, read their reports and merge into a single architecture review. Deduplicate findings that appear in both agent reports. Cross-reference findings to identify systemic issues (e.g., a layer violation that also causes include-chain bloat).

### 2.5. Synthesize Recommendation

After deduplicating findings, provide an opinionated recommendation: what is the single most impactful architectural improvement? Be specific — name the modules, the proposed change, and why it matters most. The user wants a strong read, not just a list.

### 3. Output Consolidated Report

```
## Architecture Review: [target path]

### Overview
[3-5 sentence summary of architectural health, key strengths, and primary concerns]

### Dependency Structure
[Consolidated findings from Agent A]
- Include-graph shape (hubs, worst transitive closures)
- Circular or near-circular chains
- Missing ExternalHeaders.h usage
- Dependency classification (in-process / local-sub / remote-owned / external)

### Patterns, Cohesion & Engine Invariants
[Consolidated findings from Agent B]
- Layer integrity
- Manager patterns
- Collection shape (SharedMembers/ClientMembers, phase placement, cohesion)
- Client/Server separation
- Determinism
- Thread model
- Frame-phase alignment
- Shader/CPU consistency
- Deep-module cohesion notes

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

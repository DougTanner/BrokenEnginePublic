---
name: external-architecture-review
description: Performs a multi-perspective architectural review of a codebase area, launching parallel analysis agents and consolidating into a single report. Only invoke when the user explicitly requests it (e.g., "/external-architecture-review", "run an architecture review") or when another skill explicitly instructs it. Never trigger autonomously from general code questions or during routine code changes.
allowed-tools: [Read, Grep, Glob, Task]
---

# Architecture Review

Performs a comprehensive architectural review of a specified codebase area using parallel analysis agents. Designed for a C++23 data-oriented Vulkan game engine with Common → Engine → Projects layering. Applies the "deep modules" lens (John Ousterhout): a well-designed module has a small interface hiding significant complexity. Shallow modules (large interface, thin implementation) are a code smell.

## Arguments

The user provides a target path (file or directory) to review. If no path is given, ask for one.

**Recursion**: By default, recurse into sub-directories. If the caller specifies "non-recursive" or "only files directly in this directory", pass this constraint to each subagent so they use non-recursive glob patterns (e.g., `path/*.h` instead of `path/**/*.h`).

## Instructions

### 1. Launch Parallel Analysis Agents

Use the Task tool to launch three subagents in parallel. Each agent receives the target path (and any recursion constraint) and produces a focused report.

#### Agent A: Dependency & Include Analysis (subagent_type: Explore)

Prompt the agent to:
- Map `#include` dependencies for all `.h` and `.cpp` files in the target area
- Identify headers included but not used (no symbols referenced)
- Flag transitive includes that should be made direct
- Detect circular or near-circular include chains
- Check that `Common/ExternalHeaders.h` is used for standard library headers (not individual files)
- Report include depth: how many transitive headers does each file pull in?
- Classify dependencies into categories:
  - In-process: pure computation, in-memory state, no I/O
  - Local-substitutable: dependencies with local stand-ins
  - Remote-but-owned: own services across boundaries (Ports & Adapters)
  - True external: third-party libraries/services (mock boundary)

#### Agent B: Pattern Compliance & Layer Integrity (subagent_type: Explore)

Prompt the agent to:
- **Layer violations**: Check for `Projects/` → `Engine/` internal dependencies, `Engine/` → game-specific type dependencies (except via `game::gpGame`), and `Common/` → `Engine/` or `Projects/` dependencies
- **Manager patterns**: Verify `gp*` naming, singleton access patterns, and check for unnecessary cross-manager references
- **Collection structure**: Find collections with >15 members (candidates for splitting), members in wrong phase (interpolated vs. post-render), and missing `SharedMembers()`/`ClientMembers()`/`Members()` pattern compliance
- **Client/Server guards**: Find code that should differ between `BT_CLIENT`/`BT_SERVER` but doesn't, and `#ifdef` guards that are too broad or too narrow
- **Memory patterns**: Flag heap allocations in per-frame code that should use workbuffer, and missing `ScopedSuppressAllocationTracking` for unavoidable heap use

#### Agent C: Coupling & Cohesion Analysis (subagent_type: Explore)

Prompt the agent to:
- Analyze function/class sizes — flag functions over 100 lines and files over 1000 lines
- Measure coupling: how many other files/systems does each file depend on?
- Assess cohesion: does each file/class have a single clear responsibility?
- Identify god-classes or god-managers (too many responsibilities)
- Find feature envy (code that manipulates another module's data more than its own)
- Check for proper data-oriented design: arrays of structs that should be structs of arrays, or vice versa
- Explore organically and note friction:
  - Where does understanding one concept require bouncing between many small files?
  - Where are modules so shallow that the interface is nearly as complex as the implementation?
  - Where do tightly-coupled modules create integration risk in the seams between them?

### 2. Consolidate Results

After all agents complete, read their reports and merge into a single architecture review. Deduplicate findings that appear in multiple agent reports. Cross-reference findings to identify systemic issues (e.g., a layer violation that also causes include chain bloat).

### 2.5. Synthesize Recommendation

After deduplicating findings, provide an opinionated recommendation: what is the single most impactful architectural improvement? Be specific — name the modules, the proposed change, and why it matters most. The user wants a strong read, not just a list.

### 3. Output Consolidated Report

```
## Architecture Review: [target path]

### Overview
[3-5 sentence summary of architectural health, key strengths, and primary concerns]

### Dependency & Include Analysis
[Consolidated findings from Agent A]
- Include depth summary (worst offenders)
- Unused includes
- Circular dependencies
- Missing ExternalHeaders.h usage

### Pattern Compliance
[Consolidated findings from Agent B]
- Layer violations
- Manager pattern issues
- Collection structure issues
- Client/Server guard issues
- Memory pattern violations

### Coupling & Cohesion
[Consolidated findings from Agent C]
- Oversized files/functions
- High-coupling files
- Low-cohesion modules
- DOD compliance

### Cross-Cutting Concerns
[Systemic issues that span multiple categories]

### Prioritized Recommendations
1. [Highest priority] — [justification] — [effort estimate]
2. ...
3. ...

### Architecture Health: [HEALTHY / MINOR CONCERNS / NEEDS ATTENTION / CRITICAL]
[Brief justification]
```

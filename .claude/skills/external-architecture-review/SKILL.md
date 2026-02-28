---
name: external-architecture-review
description: Performs a multi-perspective architectural review of a codebase area, launching parallel analysis agents and consolidating into a single report.
allowed-tools: [Read, Grep, Glob, Task]
---

# Architecture Review

Performs a comprehensive architectural review of a specified codebase area using parallel analysis agents. Designed for a C++23 data-oriented Vulkan game engine with Common → Engine → Projects layering.

## Arguments

The user provides a target path (file or directory) to review. If no path is given, ask for one.

## Instructions

### 1. Launch Parallel Analysis Agents

Use the Task tool to launch three subagents in parallel. Each agent receives the target path and produces a focused report.

#### Agent A: Dependency & Include Analysis (subagent_type: Explore)

Prompt the agent to:
- Map `#include` dependencies for all `.h` and `.cpp` files in the target area
- Identify headers included but not used (no symbols referenced)
- Flag transitive includes that should be made direct
- Detect circular or near-circular include chains
- Check that `Common/ExternalHeaders.h` is used for standard library headers (not individual files)
- Report include depth: how many transitive headers does each file pull in?

#### Agent B: Pattern Compliance & Layer Integrity (subagent_type: Explore)

Prompt the agent to:
- **Layer violations**: Check for `Projects/` → `Engine/` internal dependencies, `Engine/` → game-specific type dependencies (except via `game::gpGame`), and `Common/` → `Engine/` or `Projects/` dependencies
- **Manager patterns**: Verify `gp*` naming, singleton access patterns, and check for unnecessary cross-manager references
- **Collection structure**: Find collections with >15 members (candidates for splitting), members in wrong phase (interpolated vs. post-render), and missing `SharedMembers()`/`ClientMembers()`/`Members()` pattern compliance
- **Client/Server guards**: Find code that should differ between `BT_CLIENT`/`BT_SERVER` but doesn't, and `#ifdef` guards that are too broad or too narrow
- **Memory patterns**: Flag heap allocations in per-frame code that should use workbuffer, and missing `ScopedSuppressAllocationTracking` for unavoidable heap use

#### Agent C: Coupling & Cohesion Analysis (subagent_type: general-purpose)

Prompt the agent to:
- Analyze function/class sizes — flag functions over 100 lines and files over 1000 lines
- Measure coupling: how many other files/systems does each file depend on?
- Assess cohesion: does each file/class have a single clear responsibility?
- Identify god-classes or god-managers (too many responsibilities)
- Find feature envy (code that manipulates another module's data more than its own)
- Check for proper data-oriented design: arrays of structs that should be structs of arrays, or vice versa

### 2. Consolidate Results

After all agents complete, read their reports and merge into a single architecture review. Deduplicate findings that appear in multiple agent reports. Cross-reference findings to identify systemic issues (e.g., a layer violation that also causes include chain bloat).

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

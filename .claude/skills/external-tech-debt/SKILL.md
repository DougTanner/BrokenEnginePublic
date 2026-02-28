---
name: external-tech-debt
description: Scans a codebase area and produces a prioritized inventory of technical debt with effort estimates and recommended remediation order.
allowed-tools: [Read, Grep, Glob, Task]
---

# Technical Debt Assessment

Produces a prioritized technical debt inventory for a C++23 data-oriented Vulkan game engine codebase.

## Arguments

The user provides a target path (file or directory) to assess. If no path is given, ask for one.

## Instructions

### 1. Survey the Target Area

Use Glob to enumerate all `.h`, `.cpp`, and `CLAUDE.md` files in the target path. Read `CLAUDE.md` files first to understand the subsystem's intended architecture. Then sample source files, prioritizing larger files and headers.

### 2. Assess Debt Categories

Evaluate the target area against each category below. Use Grep and Read to find concrete evidence.

#### A. Dead Code & Unused Dependencies
- Unused functions, variables, types, or enum values
- `#include` directives for headers whose contents aren't referenced
- Commented-out code blocks
- Unused `#ifdef` branches

#### B. Code Duplication
- Copy-pasted logic within or across files
- Near-identical functions that differ only in type or minor details (candidates for templates)
- Boilerplate that could use existing `Common/` utilities

#### C. Include Chain Complexity
- Headers that pull in large transitive dependency chains
- Files that include more than they need (include what you use)
- Circular or near-circular include dependencies

#### D. Collection & Data Structure Bloat
- Collections with many members that could be split into focused sub-collections
- Members stored in interpolated collections that don't need interpolation (should be in post-render)
- Oversized structs passed by value

#### E. Manager Coupling
- Managers that reference each other beyond what's necessary
- Initialization order dependencies that aren't documented
- God-manager anti-pattern (one manager doing too many things)

#### F. Client/Server Build Awareness
- Code compiled identically for both `BT_CLIENT` and `BT_SERVER` that should differ
- Client-only code not guarded by `#ifdef BT_CLIENT`
- Unnecessarily duplicated logic between client and server paths
- `#ifdef` guards that are wider than necessary (entire functions vs. specific blocks)

#### G. Layer Violations
- `Projects/` code reaching into `Engine/` internals instead of using public APIs
- `Engine/` code depending on game-specific types (except via `game::gpGame`)
- `Common/` code depending on `Engine/` or `Projects/`

#### H. Shader/CPU Code Consistency
- Shader constants or layouts that have diverged from their C++ counterparts
- Magic numbers in shaders that should reference shared definitions

### 3. Prioritize Findings

Categorize each finding into one of three tiers:

- **Quick Wins** (< 15 min each) — Dead code removal, unused include cleanup, simple pattern fixes
- **Medium Effort** (15 min – 2 hours) — Extract duplicated code, split oversized functions, fix layer violations
- **Architectural** (> 2 hours) — Collection restructuring, manager decoupling, major refactors

### 4. Report

Output a structured report:

```
## Technical Debt Assessment: [target path]

### Overview
[2-3 sentence summary of the area's overall health]

### Findings by Category

#### A. Dead Code & Unused Dependencies
[Items with file:line locations, or "Clean"]

#### B. Code Duplication
[Items with all locations]

#### C. Include Chain Complexity
[Items with file locations]

#### D. Collection & Data Structure Bloat
[Items with file:line locations]

#### E. Manager Coupling
[Items with details]

#### F. Client/Server Build Issues
[Items with file:line locations]

#### G. Layer Violations
[Items with file:line locations]

#### H. Shader/CPU Consistency
[Items with file:line locations]

### Prioritized Remediation Plan

#### Quick Wins
1. [Description] — [file:line] — Est. effort
2. ...

#### Medium Effort
1. [Description] — [files affected] — Est. effort
2. ...

#### Architectural
1. [Description] — [scope] — Est. effort — [risks/dependencies]
2. ...

### Debt Score: [LOW / MODERATE / HIGH / CRITICAL]
[Brief justification]
```

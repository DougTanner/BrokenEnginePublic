---
name: reduce-file
description: Analyzes a C++ file that exceeds size guidelines and produces a plan for refactoring or splitting it into smaller files. Use when a .h/.cpp file exceeds 500-1000 lines. Only invoke when the user explicitly requests it (e.g., "/reduce-file", "this file is too big", "split this file") or when another skill explicitly instructs it. Never trigger autonomously from general code questions or during routine code changes.
allowed-tools: [Read, Grep, Glob, Bash]
user-invocable: true
---

# Reduce File

Analyzes a C++ source file that exceeds the project's size guidelines (500-1000 lines) and produces a structured plan for reducing it — either through extracting helpers to utility files, extracting new classes, or splitting struct implementations across multiple `.cpp` files.

## Key Principles

- **Free functions / helpers**: Extract to `*Utils.h`/`*Utils.cpp` files. This is the lightest-weight option and should be considered first.
- **Classes** (instance methods, member data): Each class gets its own `.h` and `.cpp` pair. The goal is to identify groups of data + behavior that form a natural class, then move that data and its methods into a new class that the original class delegates to.
- **Structs with static methods** (e.g., SOA collections): Can be split across multiple `.cpp` files sharing a single `.h`, organized by responsibility (core, update, render). The struct definition stays in one header; each `.cpp` file implements a subset of the static methods. **Important:** This applies ONLY to structs with static methods. Classes (with instance methods and member data) must NEVER be split across multiple `.cpp` files — use Option B (Extract New Classes) instead.

## Arguments

The user provides a file path as the argument:
```
/reduce-file Projects/BrokenEngineSandbox/Source/Game.cpp
```

If no argument is provided, ask the user which file to analyze.

## Instructions

### 1. Measure the File

Count total lines and determine severity:
- **500-1000 lines**: Look for refactoring opportunities (soft guideline)
- **Over 1000 lines**: File should be split (hard guideline, requires human approval of split plan)

Report the line count upfront.

### 1.5. Consider Alternatives

Before analyzing the file structure, ask the user:
- Have they considered other approaches to reducing this file?
- Are there constraints that favor one splitting strategy over others?
- Is there a preferred direction (e.g., "I want to keep the core logic here and extract helpers")?

This ensures the analysis aligns with the user's intent rather than assuming a direction.

### 2. Map the File Structure

Build a complete map of the file's contents. For each function/method/struct, record:
- **Name** and line range (start-end)
- **Approximate line count**
- **Scope guards**: Is it inside `#ifdef BT_CLIENT`, `#ifdef BT_SERVER`, or unguarded (shared)?
- **Access pattern**: Is it `static`, a free function, a method, a class definition?

Also identify:
- **Anonymous namespace items** (structs, constants, helper functions)
- **Constants and global definitions**
- **Include directives**

### 3. Identify Responsibilities

Group the functions into logical responsibilities based on:
- **Naming patterns**: Methods with shared prefixes (e.g., `Reconcile*`, `*Server`)
- **`#ifdef` boundaries**: Code gated by the same preprocessor guard
- **Data coupling**: Functions that operate on the same struct or member variables
- **Call chains**: Functions that primarily call each other

For each responsibility group, calculate:
- Total line count
- Whether it's client-only, server-only, or shared
- Key data types it operates on
- **Which member variables** the group reads/writes (critical for identifying what data moves to the new class)

### 4. Identify Shared Symbols

Find symbols (functions, constants, structs) defined in this file that are used across multiple responsibility groups. These are the "seams" that need special handling during a split:
- **Free functions in anonymous namespaces** called from multiple groups
- **Constants** used across groups
- **Local structs** used across groups

For each shared symbol, note which groups use it and propose where it should live after the split (stay in original file, move to header, move to a specific new file).

### 5. Analyze Dependencies

For each responsibility group, determine what includes it needs:
- Which headers are required by the functions in that group?
- Are there any circular dependencies that would complicate a split?

### 6. Propose Options

Present options in priority order. Not all options apply to every file — include only those that are relevant.

#### Option A: Extract Helper Functions to Utils Files (preferred for free functions)

Identify free functions, anonymous namespace helpers, utility logic, shared constants, and local structs that can be extracted to `*Utils.h`/`*Utils.cpp` files.

- **Check for existing `*Utils` files** in the same directory first — add to those before creating new ones
- **Create new `*Utils.h`/`*Utils.cpp`** if none exist
- **Existing pattern examples**: `FrameUtils.h`, `GraphicsUtils.h`, `TerrainUtils.h`, `MenuUtils.h`
- Move shared constants and structs to utils files where appropriate
- Candidates: free functions, anonymous namespace helpers that aren't tightly coupled to the class, computation helpers, formatting/conversion logic

#### Option B: Extract New Classes (when responsibilities are clearly distinct)

For each proposed new class:
- **Class name**: Following existing naming conventions
- **New files**: `ClassName.h` and `ClassName.cpp`
- **Extracted data**: Which member variables move from the original class to the new class
- **Extracted methods**: Which methods move to the new class
- **Line count estimate**: How many lines the new `.h` and `.cpp` will have
- **Scope guard**: Whether the files are wrapped in `#ifdef`
- **Includes needed**: What headers the new files require
- **Delegation pattern**: How the original class uses the new class (owns it as a member? pointer? global?)
- **Implementation order**: Break into incremental steps where the program compiles and works after each step (e.g., 1. Create new file with class shell, 2. Move first method group, 3. Move data members, 4. Update callers)

Also state:
- What remains in the original class and its reduced line count
- How the original class's header changes (removed members, new includes/forward declarations)

The extracted class must be a genuine abstraction — it should own the data it operates on and present a meaningful interface. Don't create a class that just wraps free functions with no state.

#### Option C: Split Struct Implementation Across Multiple .cpp Files (structs with static methods ONLY — never classes)

This applies to structs whose interface is a set of static methods (common for SOA collections). The struct definition stays in a single `.h`; the static method implementations are split across multiple `.cpp` files by responsibility.

For each proposed `.cpp` file:
- **File name**: `StructName<Responsibility>.cpp` (e.g., `BlastersUpdate.cpp`, `BlastersRender.cpp`)
- **Methods moved**: Which static methods go into this file
- **Line count estimate**: How many lines the new `.cpp` will have
- **Scope guard**: Whether the file is wrapped in `#ifdef`
- **Includes needed**: What headers the new file requires
- **Implementation order**: Break into incremental steps where the program compiles after each step (e.g., 1. Create new .cpp, 2. Move first method group, 3. Update vcxproj and vcxproj.filters (filter must mirror on-disk directory), 4. Verify build)

Also state:
- What remains in the original `.cpp` and its reduced line count
- The `.h` file does not change (all methods remain declared there)

### 7. Highlight Risks

Flag any complications:
- Functions that straddle responsibility boundaries
- Shared mutable state between groups
- Template or inline functions that must stay in headers
- `friend` declarations that create coupling
- Virtual method overrides that must stay together
- Data that is tightly coupled across groups (hard to separate into distinct classes)

### 8. Decision Document

Summarize the key decisions in the chosen option. Reference modules and responsibilities, not specific file paths (which become outdated):
- **Modules affected**: Which logical modules change
- **Interfaces changed**: New public APIs introduced, old ones removed
- **Architectural decisions**: Why this split boundary was chosen
- **Out of scope**: What was considered but explicitly excluded from this refactoring

## Output Format

## File Analysis: `<filename>`

**Lines**: `<count>` (`<severity>`)

### Responsibility Groups

| # | Responsibility | Lines | Scope | Key Types | Member Variables |
|---|---------------|-------|-------|-----------|-----------------|
| 1 | `<name>`      | ~`<n>` | shared/client/server | `<types>` | `<vars>` |
| 2 | `<name>`      | ~`<n>` | shared/client/server | `<types>` | `<vars>` |

### Shared Symbols
- `<symbol>` (`<type>`) — used by groups `<X, Y>`. Recommendation: `<action>`

### Dependencies
- **Group `<name>`**: requires `<headers>`. Circular dependency risks: `<none or description>`

### Option A: Extract to Utils Files (preferred)

**Target file**: `<ExistingOrNewUtils>.h` / `<ExistingOrNewUtils>.cpp`
- `<function/constant/struct to extract>`
- `<function/constant/struct to extract>`

**Target file**: `<AnotherUtils>.h` / `<AnotherUtils>.cpp` (if needed)
- `<function/constant/struct to extract>`

**Estimated result**: ~`<n>` lines (down from `<original>`)

### Option B: Extract New Classes

#### `<ClassName>` (`<ClassName>.h` / `<ClassName>.cpp`)
- **Purpose**: `<what this class represents>`
- **Extracted data**: `<member variables that move to this class>`
- **Extracted methods**: `<methods that become methods of this class>`
- **~Lines**: `<h lines>` + `<cpp lines>`
- **Scope**: `<shared/client/server>`
- **Delegation**: `<how the original class uses this — member, pointer, global, etc.>`

**Original class after extraction**:
- **Removed members**: `<list>`
- **New members/includes**: `<list>`
- **~Lines**: `<h lines>` + `<cpp lines>` (down from `<original>`)

### Option C: Split Struct Implementation

**Header** (unchanged): `<StructName>.h`

#### `<StructName><Responsibility>.cpp`
- **Methods**: `<static methods in this file>`
- **~Lines**: `<n>`
- **Scope**: `<shared/client/server>`

#### `<StructName><Responsibility2>.cpp`
- **Methods**: `<static methods in this file>`
- **~Lines**: `<n>`
- **Scope**: `<shared/client/server>`

**Original `.cpp` after split**: ~`<n>` lines (down from `<original>`)

### Shared Symbol Resolution
- `<symbol>`: `<where it goes and why>`

### Risks
- `<risk 1>`
- `<risk 2>`

---

After presenting the analysis, ask the user which option they'd like to proceed with. If they choose a class extraction or struct split, the output can be used directly as input to a planning document.

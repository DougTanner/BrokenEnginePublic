---
name: reduce-file
description: Analyzes a C++ file that exceeds size guidelines and produces a plan for refactoring or splitting it into smaller files. Use when a .h/.cpp file exceeds 500-1000 lines.
allowed-tools: [Read, Grep, Glob, Bash, Task]
user-invocable: true
---

# Reduce File

Analyzes a C++ source file that exceeds the project's size guidelines (500-1000 lines) and produces a structured plan for reducing it — either through extracting helpers to utility files or extracting new classes.

## Key Principle

- **Classes** (instance methods, member data): Each class gets its own `.h` and `.cpp` pair. Reducing a file means extracting cohesive responsibilities into new classes — not splitting one class's method implementations across multiple `.cpp` files. The goal is to identify groups of data + behavior that form a natural class, then move that data and its methods into a new class that the original class delegates to.

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

Present two categories of options in priority order:

#### Option A: Extract Helper Functions to Utils Files (preferred)

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

Also state:
- What remains in the original class and its reduced line count
- How the original class's header changes (removed members, new includes/forward declarations)

**Important**: The extracted class must be a genuine abstraction — it should own the data it operates on and present a meaningful interface. Don't create a class that just wraps free functions with no state.

### 7. Highlight Risks

Flag any complications:
- Functions that straddle responsibility boundaries
- Shared mutable state between groups
- Template or inline functions that must stay in headers
- `friend` declarations that create coupling
- Virtual method overrides that must stay together
- Data that is tightly coupled across groups (hard to separate into distinct classes)

## Output Format

```
## File Analysis: <filename>

**Lines**: <count> (<severity>)

### Responsibility Groups

| # | Responsibility | Lines | Scope | Key Types | Member Variables |
|---|---------------|-------|-------|-----------|-----------------|
| 1 | <name>        | ~<n>  | shared/client/server | <types> | <vars> |
| 2 | <name>        | ~<n>  | shared/client/server | <types> | <vars> |
| ...

### Shared Symbols
- `<symbol>` (<type>) — used by groups <X, Y>. Recommendation: <action>

### Option A: Extract to Utils Files (preferred)

**Target file**: `<ExistingOrNewUtils>.h` / `<ExistingOrNewUtils>.cpp`
- <function/constant/struct to extract>
- <function/constant/struct to extract>

**Target file**: `<AnotherUtils>.h` / `<AnotherUtils>.cpp` (if needed)
- <function/constant/struct to extract>

**Estimated result**: ~<n> lines (down from <original>)

### Option B: Extract New Classes

#### `<ClassName>` (`<ClassName>.h` / `<ClassName>.cpp`)
- **Purpose**: <what this class represents>
- **Extracted data**: <member variables that move to this class>
- **Extracted methods**: <methods that become methods of this class>
- **~Lines**: <h lines> + <cpp lines>
- **Scope**: <shared/client/server>
- **Delegation**: <how the original class uses this — member, pointer, global, etc.>

#### `<ClassName2>` ...

**Original class after extraction**:
- **Removed members**: <list>
- **New members/includes**: <list>
- **~Lines**: <h lines> + <cpp lines> (down from <original>)

### Shared Symbol Resolution
- `<symbol>`: <where it goes and why>

### Risks
- <risk 1>
- <risk 2>
```

After presenting the analysis, ask the user which option they'd like to proceed with. If they choose a class extraction, the output can be used directly as input to a planning document.

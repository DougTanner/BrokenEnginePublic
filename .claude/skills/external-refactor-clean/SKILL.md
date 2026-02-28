---
name: external-refactor-clean
description: Analyzes C++ code for dead code, duplication, unnecessary complexity, and refactoring opportunities. Reports findings with locations and suggested improvements.
allowed-tools: [Read, Grep, Glob, Task]
---

# Code Refactoring Analysis

Analyzes specified code areas for refactoring opportunities specific to a C++23 data-oriented Vulkan game engine.

## Arguments

The user provides a target path (file or directory) to analyze. If no path is given, ask for one.

## Instructions

### 1. Scan the Target Area

Use Glob and Read to enumerate and examine all `.h` and `.cpp` files in the target path. For large directories, prioritize files by size (larger files tend to accumulate more debt).

### 2. Identify Dead Code

Search for:
- **Unused functions/methods** — declared but never called (use Grep to search for call sites across the codebase)
- **Unused includes** — `#include` directives where nothing from that header is referenced in the file
- **Commented-out code blocks** — large blocks of `//` or `/* */` commented code that should be removed
- **Unused variables/parameters** — declared but never read
- **Unreachable code** — code after unconditional `return`, `break`, or `continue`

### 3. Detect Duplication

Look for:
- **Copy-pasted logic** — similar code blocks across files or within the same file that could be extracted into a shared function
- **Repeated patterns** — boilerplate that could be simplified with a template, macro, or utility function from `Common/`
- **Parallel structures** — collections or managers that follow nearly identical patterns and could share a common base or template

### 4. Assess Complexity

Flag:
- **Oversized functions** — functions exceeding ~100 lines that could be decomposed
- **Deep nesting** — more than 3 levels of indentation from control flow
- **Long parameter lists** — functions taking more than 5-6 parameters (consider a struct)
- **Unnecessary abstraction** — wrapper classes or indirection that add complexity without value
- **Heap allocation in hot paths** — `std::vector`, `std::string`, or `new` in per-frame code where workbuffer (`gpThreadLocal->mWorkbuffer`) should be used instead

### 5. Check Engine-Specific Patterns

- **Bool proliferation** — multiple `bool` parameters or members that should use `common::Flags<EnumType>`
- **Unaligned math types** — `Float4` where `Float4A` (aligned) should be used for SIMD performance
- **Missing workbuffer usage** — temporary allocations using heap when `gpThreadLocal->mWorkbuffer` is available
- **Layer violations** — `Projects/` code reaching into `Engine/` internals, or `Engine/` code depending on specific game types (except via `game::gpGame`)
- **`#ifdef BT_CLIENT`/`BT_SERVER` scope** — client-only code that isn't properly guarded, or guards that are wider than necessary

### 6. Report Findings

Output a structured report:

```
## Refactoring Analysis: [target path]

### Dead Code
[List items with file:line locations, or "None found"]
- file:line — Description of dead code

### Duplication
[List duplicated patterns with all locations]
- Description — file1:line, file2:line

### Complexity Issues
[List with file:line locations]
- file:line — Description and suggested simplification

### Engine Pattern Violations
[List with file:line locations]
- file:line — Description and recommended pattern

### Summary
- Total issues found: N
- Quick wins (< 5 min each): [list]
- Medium effort: [list]
- Larger refactors: [list]

### Recommendation
[Brief overall assessment and suggested priority order]
```

---
name: external-refactor-clean
description: Analyzes C++ code for *in-function* refactoring opportunities — complexity, nesting depth, oversized functions, heap-in-hot-path, bool-proliferation, unaligned math types. Hands oversized files to `/reduce-file`; defers dependency-shape, layer, and cohesion issues to `/external-architecture-review`. Manual-only — run `/external-refactor-clean <path>`; also chained as Phase 2 of `/external-deep-analysis`.
disable-model-invocation: true
allowed-tools: [Read, Grep, Glob, Bash]
---

# Refactor Clean (in-function mechanics)

Analyzes specified code for in-function refactoring opportunities specific to a C++23 data-oriented Vulkan game engine.

**Scope boundary:** This skill owns what happens *inside* functions and structs. It does NOT flag layer violations, include-chain complexity, cross-file duplication, or dependency-shape issues — those belong to `/external-architecture-review`. Oversized files (>500–1000 lines) are handed off to `/reduce-file` rather than analyzed here.

## Arguments

The user provides a target path (file or directory) to analyze. If no path is given, ask for one.

**Recursion**: By default, recurse into sub-directories. If the caller specifies "non-recursive" or "only files directly in this directory", use non-recursive glob patterns (e.g., `path/*.h` instead of `path/**/*.h`).

## Instructions

### 0. Read Authorities

Before scanning, read:
- Repo root `CLAUDE.md` (KISS/YAGNI/DRY, key patterns)
- Nearest nested `CLAUDE.md` for the target path
- `Documents/C++StyleGuide.txt`

Cite these documents in findings; the checklists below are detection heuristics, not the authority.

### 1. Scan the Target Area

Use Glob to enumerate `.h` and `.cpp` files in the target path — recursive globs (`**/*.h`) by default, non-recursive (`*.h`) if the caller requested non-recursive mode. Get line counts with Bash `wc -l`; for large directories, analyze the largest files first.

### 2. File-Size Triage (handoff only)

For each file exceeding 500 lines (header) or 1000 lines (implementation), **do not analyze in detail** — list it in the output with a recommendation to run `/reduce-file <path>`. Continue with the remaining files.

### 3. Complexity Within Functions

Flag:
- **Oversized functions** — exceeding ~100 lines that could be decomposed
- **Deep nesting** — more than 3 levels of indentation from control flow
- **Long parameter lists** — more than 5–6 parameters (consider a struct)
- **Unreachable code** — after unconditional `return`/`break`/`continue`
- **Unused parameters / local variables** — declared but never read
- **Unnecessary abstraction** — wrapper classes or indirection adding complexity without value

### 4. Hot-Path Allocation

Flag:
- **Heap allocation in per-frame code** — local `std::vector`/`std::string`/`new` where `gpThreadLocal->mWorkbuffer` should be used instead
- **Unavoidable heap without `ScopedSuppressAllocationTracking`** + `// Heap:` comment (see `Engine/Source/Memory/CLAUDE.md`)
- **Float format specs in `LOG(...)`** — allocation-tracked code only (Game and Engine; not the offline DataPacker): `{:.Nf}`/`{:e}` heap-allocate and trip the allocation tracker; wrap with `common::Wb`/`WbV2/V3/V4` (repo-code-review skill §2b)

### 5. Engine Micro-Patterns

Flag:
- **Bool proliferation** — multiple `bool` parameters or members that should use `common::Flags<EnumType>`
- **Unaligned DirectX Math types** — `Float4` where `Float4A` (aligned) is available
- **DirectX Math operators** — `vec + vec`, `f * vec`, `-vec` instead of function forms (`XMVectorAdd`/`Scale`/`Negate`)
- **Over-wide `#ifdef BT_CLIENT`/`BT_SERVER`** — guards wider than the code that actually differs. Move the guard inward.
- **Standard-header placement** — new `#include <header>` in a source file; should be in `Common/ExternalHeaders.h`
- **`*Base` references** — engine code naming `GameBase`/`CameraBase`/other `*Base` types outside the base file itself; use the game-derived versions via `game::gpGame`/`game::gp*`. Do not flag engine *reading* `game::gp*` globals — that is by design, not a layer violation (see `Engine/Source/CLAUDE.md`)

### 6. Report Findings

Output a structured report (default template — omit sections with no findings):

```
## Refactor-Clean Analysis: [target path]

### File-Size Triage (delegate to /reduce-file)
- file — N lines — run `/reduce-file <path>`

### Complexity Issues
[List with file:line locations]
- file:line — Description and suggested simplification

### Hot-Path Allocation
[List with file:line locations]

### Engine Micro-Patterns
[List with file:line locations and recommended pattern]

### Summary
- Total issues found: N
- Quick wins (< 5 min each): [list]
- Medium effort: [list]
- Larger refactors: [list]

### Recommendation
[Brief overall assessment and suggested priority order]
```

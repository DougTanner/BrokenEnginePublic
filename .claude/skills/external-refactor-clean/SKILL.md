---
name: external-refactor-clean
description: Analyzes C++ code for *in-function* refactoring opportunities — complexity, nesting depth, oversized functions, heap-in-hot-path, bool-proliferation, unaligned math types, plus the in-function residue of iteratively AI-generated code (phantom guards, swallowed errors, return-type inconsistency, boundary-condition gaps). Hands oversized files to `/reduce-file`; defers dependency-shape, layer, and cohesion issues to `/external-architecture-review`. Manual-only — run `/external-refactor-clean <path>`; also chained as Phase 2 of `/external-deep-analysis`.
disable-model-invocation: true
allowed-tools: [Read, Grep, Glob, Bash, Agent]
---

# Refactor Clean (in-function mechanics)

Analyzes specified code for in-function refactoring opportunities specific to a C++23 data-oriented Vulkan game engine. Because this codebase was built through iterative AI-generation sessions, the scan also hunts the in-function residue that process characteristically leaves. Security auditing is deliberately out of scope — every finding here is structural or correctness-only; injection, auth, secrets, crypto, and CORS belong to a dedicated security pass, not this skill.

**Scope boundary:** This skill owns what happens *inside* functions and structs. It does NOT flag layer violations, include-chain complexity, cross-file duplication, or dependency-shape issues — those belong to `/external-architecture-review`. Oversized files (>500–1000 lines) are handed off to `/reduce-file` rather than analyzed here.

## Arguments

The user provides a target path (file or directory) to analyze. If no path is given, ask for one.

**Recursion**: By default, only analyze files directly in the specified directory (non-recursive) — use non-recursive glob patterns (e.g., `path/*.h`). Recurse into sub-directories only when the caller explicitly requests it ("recursive", "include subdirectories") — then use recursive patterns (`path/**/*.h`).

## Instructions

### 0. Read Authorities

Before scanning, read:
- Repo root `CLAUDE.md` (KISS/YAGNI/DRY, key patterns)
- Nearest nested `CLAUDE.md` for the target path
- `Documents/C++StyleGuide.txt`

Cite these documents in findings; the checklists below are detection heuristics, not the authority.

### 1. Scan the Target Area

Use Glob to enumerate `.h` and `.cpp` files in the target path — non-recursive globs (`*.h`) by default, recursive (`**/*.h`) if the caller requested recursion. Get line counts with Bash `wc -l`.

### 2. File-Size Triage (handoff only)

For each file exceeding 500 lines (header) or 1000 lines (implementation), **do not analyze in detail** — list it in the output with a recommendation to run `/reduce-file <path>`. Continue with the remaining files.

### 3. Fan-Out for Large Targets

If more than ~15 files or ~8,000 lines remain after triage, do not scan inline — partition the remaining files into batches of related functionality (same subsystem, sibling files) and launch one subagent per batch in parallel (`subagent_type: "general-purpose"`, `model: "fable"`). Each subagent prompt includes: the step-0 authority list to read first, the full checklists from steps 4–7, its file batch, and the instruction to return findings as a structured list with `file:line` locations and the checklist category for each. Consolidate and dedup the returned findings, then continue at step 8. At or below the threshold, scan inline yourself (steps 4–7).

### 4. Complexity Within Functions

Flag:
- **Oversized functions** — exceeding ~100 lines that could be decomposed
- **Deep nesting** — more than 3 levels of indentation from control flow
- **Long parameter lists** — more than 5–6 parameters (consider a struct)
- **Unreachable code** — after unconditional `return`/`break`/`continue`
- **Unused parameters / local variables** — declared but never read
- **Unnecessary abstraction** — wrapper classes or indirection adding complexity without value

### 5. Hot-Path Allocation

Flag:
- **Heap allocation in per-frame code** — local `std::vector`/`std::string`/`new` where `gpThreadLocal->mWorkbuffer` should be used instead
- **Unavoidable heap without `ScopedSuppressAllocationTracking`** + `// Heap:` comment (see `Engine/Source/Memory/CLAUDE.md`)
- **Float format specs in `LOG(...)`** — allocation-tracked code only (Game and Engine; not the offline DataPacker): `{:.Nf}`/`{:e}` heap-allocate and trip the allocation tracker; wrap with `common::Wb`/`WbV2/V3/V4` (repo-code-review skill §2b)

### 6. Engine Micro-Patterns

Flag:
- **Bool proliferation** — multiple `bool` parameters or members that should use `common::Flags<EnumType>`
- **Unaligned DirectX Math types** — `Float4` where `Float4A` (aligned) is available
- **DirectX Math operators** — `vec + vec`, `f * vec`, `-vec` instead of function forms (`XMVectorAdd`/`Scale`/`Negate`)
- **Over-wide `#ifdef BT_CLIENT`/`BT_SERVER`** — guards wider than the code that actually differs. Move the guard inward.
- **Standard-header placement** — new `#include <header>` in a source file; should be in `Common/ExternalHeaders.h`. Exception: the single-TU `*_IMPLEMENTATION` includes in the `ThirdParty/Prebuilts/Source/` unity `.cpp`s stay local by necessity — do not flag them
- **`*Base` references** — engine code naming `GameBase`/`CameraBase`/other `*Base` types outside the base file itself; use the game-derived versions via `game::gpGame`/`game::gp*`. Do not flag engine *reading* `game::gp*` globals — that is by design, not a layer violation (see `Engine/Source/CLAUDE.md`)

### 7. AI-Generation Anti-Patterns (in-function)

Iterative AI-generation leaves characteristic in-function residue. Hunt for structural evidence, not surface compliance — and stay non-security (structural/correctness only):

- **Phantom guards / over-specified edge cases** — checks for conditions that cannot occur given the callers (null-checks on parameters passed from within the codebase, branches for impossible enum values, redundant re-validation). They clutter core logic and give false safety. The repo already forbids them: *Error handling at trust boundaries only* (no defensive validation between our own functions) and *No useless ASSERTs* (root `CLAUDE.md`). Flag the guard; the preferred fix is making the condition impossible in the caller.
- **Swallowed / cosmetic error handling** — `try`/`catch` or result checks that log-and-continue with no recovery, rethrow, or signal to the caller; every failure collapsing to the same generic message. The code looks handled while the caller has no indication anything failed. Contrast with the trust-boundary rule: opaque results (file reads, OS / third-party / network) *must* be checked and propagated meaningfully.
- **Return-value inconsistency** — a function returning a real value on the success path and a silent default / empty / `false` on the error path without the signature (or an out-param / status) letting the caller tell them apart. Trace every return: do all paths agree on type and meaning?
- **Boundary-condition gaps** — for code processing a collection or count, trace the empty, single-element, and zero-value cases. AI-generated loops systematically assume "at least one, probably many"; flag missing empty / `nullptr` / count-of-one handling. Only flag where the case can actually occur at existing call sites — a check for a case no caller can produce is a phantom guard (above), not a gap.
- **Manual acquire without RAII** — a handle, mapping, lock, or subscription acquired without an RAII owner guaranteeing release on every path (early return, exception). The engine is RAII-everywhere; a raw acquire/release pair is the smell.
- **Narration comments** — comments restating what the next line plainly does (`// increment i`), standing in for readable code. Flag dense trivial commenting; the fix is clearer names/structure. Genuine *why* comments and the repo's required annotations (`// Heap:`, client/server guards) are not this.
- **Intra-file drift** — Hungarian appearing then vanishing, `camelCase`/`snake_case` mixed mid-file, or a stale `TODO:`/`FIXME:` never resolved: signals a later-generated block spliced into an earlier file. Note the location, but hand the actual naming/format enforcement to `/code-style-review` rather than duplicating it here.

### 8. Report Findings

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

### AI-Generation Anti-Patterns
[List with file:line locations]
- file:line — Phantom guard / swallowed error / return or boundary gap / narration comment — recommended fix

### Summary
- Total issues found: N
- Quick wins (< 5 min each): [list]
- Medium effort: [list]
- Larger refactors: [list]

### Recommendation
[Brief overall assessment and suggested priority order]
```

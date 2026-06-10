---
name: code-style-review
description: Reviews and auto-fixes C++ style violations in files modified during the session, per `Documents/C++StyleGuide.txt` — Hungarian notation, `auto` restrictions, float literal suffixes, `nullptr`, `override`, naming/abbreviation rules, brace and argument formatting. Use after any C++ code change (C++ Code Change Process step 5), or when the user asks for a style review, style check, or naming/formatting cleanup.
allowed-tools: [Read, Edit, Grep]
---

# Code Style Review

Reviews C++ files edited in this conversation and fixes style violations per `Documents/C++StyleGuide.txt`. Scope: auto-fix within the modified line ranges — not a full-file rewrite.

## Instructions

### 1. Identify Modified Files

List all `.cpp` and `.h` files you edited in this conversation (check your Edit/Write tool calls). Apply fixes only to line ranges that were modified.

### 2. Read the Style Guide

Read `Documents/C++StyleGuide.txt` first. It is the authoritative source; this skill's rule list below is a checklist of the highest-value mechanical rules that are easy to miss and easy to grep for.

### 3. Apply the High-Value Rule Checklist

These rules are mechanical and grep-able. For each, search the modified ranges and fix violations:

- **Rule 14** — use `Count` not `Num` in identifiers (`kuiThreadCount`, not `kuiNumThreads`).
- **Rule 15** — `auto` is forbidden except: XMVECTOR/XMMATRIX results, type obvious from a template parameter on the right, iterators, structured bindings. Never plain `auto` in range-based for loops. Fix: spell out the type.
- **Rule 16** — never `operator[]` on `std::unordered_map`/`std::map` (silently default-inserts): read with `at()`, write with `insert_or_assign()`, create with `try_emplace()`. Index `std::vector` with `.at()`.
- **Rule 19** — `class` in templates: use `typename`, not `class`, for type parameters.
- **Rule 21** — no `std::array`/`std::span`/`std::bitset` — use C arrays and pointers (their `operator[]`/iterators are slow in debug builds). Exception: `std::span` for pointer + size of mapped Vulkan memory.
- **Rule 27** — float literals get leading digit and `f` suffix: `1.0` → `1.0f`, `.5` → `0.5f`.
- **Rule 28** — `NULL` → `nullptr`.
- **Rule 29** — virtual overrides: add `override` keyword.
- **Rule 32** — `std::unordered_map` instead of `std::map`.
- **Rule 41** — no `using namespace std;` (`using namespace DirectX;` is allowed).
- **Rule 49** — default `public:` members; no trivial getters/setters — reserve private + setter for members that must change together to keep an invariant.
- **Rule 50** — null pointer checks: `if (pPointer != nullptr)`, not `if (pPointer)`.
- **Rule 51** — function/macro arguments all on one line, however long (exception: lambda or struct/initializer-list literal arguments).
- **Rule 52** — space before `{}` universal initializers: `uuid_t uuid {};`.
- **Rule 56** — no abbreviations in identifiers (`cmdBuf` → `rCommandBuffer`, `bufIt` → `bufferIt`); exceptions: `i`/`j`/`k` loop counters, `it` iterators.
- **Rule 57** — no `Impl`/`Internal` suffixes on function names; use scope (private, anonymous namespace) to distinguish internal versions.
- **Rule 58** — `#ifdef X` → `#if defined(X)`.

For any rule not in this list, defer to the style guide text.

### 3b. Project Conventions Beyond the Style Guide

These are project-wide conventions that complement `Documents/C++StyleGuide.txt`. Apply mechanically, same as the rule checklist above:

- **Debug instrumentation must be tag-prefixed.** Temporary `LOG(...)`, `printf`, `DEBUG_BREAK()`, or `assert(false)` added while investigating a bug must carry a unique `[DEBUG-<short-id>]` tag prefix (e.g., `[DEBUG-a4f2]`, `[DEBUG-w-leak]`) so end-of-session cleanup is a single grep — untagged debug logs survive into commits, tagged ones die. If a file modified this session contains an untagged `LOG(.*kDebug.*)` or `// FIXME` / `// HACK` clearly added during the session (recently authored, near the change site), add a `[DEBUG-<tag>]` prefix or remove the line. Do not flag pre-existing `kDebug` logs — those are intentional one-time logs per the LOG-level convention in the root `CLAUDE.md`.

### 4. Fix Policy

- Fix violations directly without asking permission — auto-apply within changed files.
- For mechanical rules (`NULL` → `nullptr`, float suffix, `override`, etc.) the risk is near-zero; apply silently.
- For **Rule 3 (Hungarian notation)** and **Rule 56 (abbreviation expansion)** renames, the risk is real: renaming an identifier referenced from outside the modified file will break callers. Apply the rename anyway — the user has accepted this tradeoff — but report every such rename in the output so the user can verify cross-file references compile. After the rename, run a follow-up Grep on the old identifier across the repo and list any remaining hits in the report.

### 5. Report

List fixes applied by rule number, and any cross-file references left over after Hungarian/abbreviation renames.

```
## Style Review Results

### Fixes Applied
- file:line — Rule N — <short description>

### Cross-File References to Verify (Hungarian / abbreviation renames)
- <old identifier> → <new identifier>
  - file:line (in another file) still uses <old identifier>
```

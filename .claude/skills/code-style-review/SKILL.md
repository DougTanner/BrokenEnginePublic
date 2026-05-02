---
name: code-style-review
description: Reviews and auto-fixes C++ style violations in files modified during the session. Enforces the rules in `Documents/C++StyleGuide.txt`, including the high-value mechanical rules listed below. Use this skill after making C++ code changes to enforce project style conventions.
allowed-tools: [Read, Edit, Grep, Glob]
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

- **Rule 15** — `auto` misuse: `auto` is forbidden except where the type is unnameable (lambdas, iterators). Fix: spell out the type.
- **Rule 16** — `operator[]` on `std::map`: replaces default-insertion with `at()` or `find()` unless insertion is desired.
- **Rule 19** — `class` in templates: use `typename`, not `class`, for type parameters.
- **Rule 21** — prefer `std::array` over C arrays.
- **Rule 27** — float literals: `1.0` → `1.0f`, `0.5` → `0.5f`.
- **Rule 28** — `NULL` → `nullptr`.
- **Rule 29** — virtual overrides: add `override` keyword.
- **Rule 32** — prefer `std::span` / `std::bitset` over raw pointers/sizes where available.
- **Rule 41** — no `using namespace std;`.
- **Rule 49** — avoid trivial getters/setters; expose members directly when access is unrestricted.
- **Rule 50** — null pointer checks: `if (pPointer != nullptr)`, not `if (pPointer)`.
- **Rule 51** — function/macro arguments on one line (exceptions: lambdas, structs with designated initializers).
- **Rule 52** — space before `{}` in braced-init lists.
- **Rule 56** — no abbreviations in identifiers (e.g., `Num` → `Count`, `Msg` → `Message`).
- **Rule 57** — no `Impl` suffix on classes.
- **Rule 58** — `#ifdef X` → `#if defined(X)`.
- **Rule 14** — use `Count` not `Num` in identifiers.

For any rule not in this list, defer to the style guide text.

### 3b. Project Conventions Beyond the Style Guide

These are project-wide conventions that complement `Documents/C++StyleGuide.txt`. Apply mechanically, same as the rule checklist above:

- **Debug instrumentation must be tag-prefixed.** Any temporary `LOG(...)`, `printf`, `DEBUG_BREAK()` cluster, or `assert(false)` left in the code as part of investigating a bug must use a unique tag prefix in the form `[DEBUG-<short-id>]` (e.g., `[DEBUG-a4f2]`, `[DEBUG-w-leak]`). The tag lets cleanup happen by single grep at the end of the session — untagged debug logs survive into commits, tagged ones die. If a modified file in this session contains an untagged `LOG(.*kDebug.*)` or commented-out `// FIXME` / `// HACK` that was clearly added during the session (recently authored, near the change site), flag it and either add a `[DEBUG-<tag>]` prefix or remove the line. Do not flag pre-existing `kDebug` logs that predate the session — those are intentional one-time logs per the LOG-level convention in the root `CLAUDE.md`.

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

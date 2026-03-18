---
name: code-style-review
description: Reviews and auto-fixes C++ style violations in files modified during the session. Use this skill after making code changes as part of the C++ code change workflow (step 6).
allowed-tools: [Read, Edit, Grep, Glob]
---

# Code Style Review

Reviews C++ files edited in this conversation and fixes style violations.

## Instructions

1. **Identify modified files**: List all `.cpp` and `.h` files you edited in this conversation (check your Edit/Write tool calls). Apply the following steps only to line ranges that were modified (not the entire file).

2. **Read `/Documents/C++StyleGuide.txt`** and fix violations in each modified file. The style guide is the authoritative source — apply all 59 rules. Pay special attention to rules 56-57 (no abbreviations, no Impl suffixes).

3. **Additionally enforce these rules** (not covered by the style guide):

### Function Call and Macro Formatting
- Keep function and macro arguments on ONE line (do not split across multiple lines) — this applies to regular function calls AND macros like FILE_LOG, ASSERT, etc.
- **Exceptions** (these SHOULD be multi-line):
  - Lambdas
  - Structs with designated initializers

### Comments
- Add a single-line comment before related code blocks explaining purpose
- **DO NOT** add comments about removed code or fixed bugs

## Notes
- Fix violations directly without asking permission
- Only review files YOU edited (not all project files)

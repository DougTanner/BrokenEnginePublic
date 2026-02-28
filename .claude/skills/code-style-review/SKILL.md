---
name: code-style-review
description: Reviews and auto-fixes C++ style violations in files modified during the session. Use this skill after making code changes as part of the C++ code change workflow (step 7).
allowed-tools: [Read, Edit, Grep, Glob]
---

# Code Style Review

Reviews C++ files edited in this conversation and fixes style violations.

## Instructions

1. **Identify modified files**: List all `.cpp` and `.h` files you edited in this conversation (check your Edit/Write tool calls), IMPORTANT: apply the following steps #2 and #2 only to line ranges that were modified in this conversation (not the entire file)

2. **Read and fix each file** applying these rules:

### Function Call Formatting
- IMPORTANT: Keep function arguments on ONE line (do not split across multiple lines)
- **Exceptions** (these SHOULD be multi-line):
  - Lambdas
  - Structs with designated initializers

### Comments
- Add a single-line comment before related code blocks explaining purpose
- Add comments when purpose isn't obvious from context
- **DO NOT** add comments about removed code or fixed bugs

3. **IMPORTANT**: Apple the full style guide at `/Documents/C++StyleGuide.txt`

## Notes
- Fix violations directly without asking permission
- Only review files YOU edited (not all project files)

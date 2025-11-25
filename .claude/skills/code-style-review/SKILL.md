---
name: code-style-review
description: Reviews and auto-fixes C++ style violations in files modified during the session. Use this skill after making code changes as part of the C++ code change workflow (step 4).
allowed-tools: [Read, Edit, Grep, Glob]
---

# Code Style Review

Reviews C++ files edited in this conversation and fixes style violations.

## Instructions

1. **Identify modified files**: List all `.cpp` and `.h` files you edited in this conversation (check your Edit/Write tool calls)

2. **Read and fix each file** applying these rules:

### Function Call Formatting
- Keep function arguments on ONE line (do not split across lines)
- **Exceptions** (these SHOULD be multi-line):
  - Lambdas
  - Structs with designated initializers
  - Pre-existing multi-line code (human-formatted) - leave unchanged

### Comments
- Add a single-line comment before related code blocks explaining purpose
- Add comments when purpose isn't obvious from context
- **DO NOT** add comments about removed code or fixed bugs

3. **IMPORTANT**: Full style guide at `/Documents/C++StyleGuide.txt`

## Notes
- Fix violations directly without asking permission
- Only review files YOU edited (not all project files)

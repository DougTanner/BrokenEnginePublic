---
name: code-style-review
description: Reviews and auto-fixes C++ style violations in files modified during the session. Use this skill after making code changes as part of the C++ code change workflow (step 4).
allowed-tools: [Read, Edit, Grep, Glob]
---

# Code Style Review

Reviews all C++ files that have been edited during this conversation session and automatically fixes any style violations according to the project's coding standards.

## When to Use

Invoke this skill after making C++ code changes, as part of step 4 in the C++ Code Change Process defined in CLAUDE.md. This ensures all modified files conform to the project's style guide.

## Instructions

1. **Identify modified files**: Review all `.cpp`, `.h`, and related C++ files that have been edited during this conversation
2. **Apply style guide**: Use the rules from @Documents/C++StyleGuide.txt
3. **Apply the following instructions**:
	- DO NOT split function calls across multiple lines - keep all function arguments on the same line as the function name
		- Except for lambdas and structs with designated initializers, split them into multiple lines
	- When adding multiple lines of code that are related add a single line comment before them explaining what they do
	- Also add comments if the purpose of any code is not obvious from the immediate context
	- DO NOT leave comments explaining what code has been removed or what bugs have been fixed
4. **Auto-fix violations**: Automatically correct all style issues found

## Important Notes

- Only review files edited in this conversation (check conversation history)
- Make fixes directly - do not ask for permission

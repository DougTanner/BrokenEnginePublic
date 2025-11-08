---
description: Review and auto-fix C++ style violations in files modified this session
model: sonnet
---

# Code Style Review

Review all C++ files that you have edited during this conversation session and automatically fix any style violations according to the project's coding standards.

## Instructions

1. **Identify modified files**: Review all `.cpp`, `.h`, and related C++ files that you have edited during this conversation
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

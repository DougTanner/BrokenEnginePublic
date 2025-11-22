---
name: code-review
description: Reviews C++ code changes for bugs, implementation correctness, and simplification opportunities. Use this skill after making code changes as part of the C++ code change workflow (step 3). (project)
allowed-tools: [Read, Grep, Glob, Task]
---

# Code Review

This skill reviews C++ code changes made during the session to ensure quality, correctness, and adherence to minimal change principles.

## When to Use

Invoke this skill as **step 3** in the C++ Code Change Process (see `/CLAUDE.md`), after:
- Making the requested code changes (step 1)
- Updating all affected locations in the codebase (step 2)

And before:
- Running code-style-review (step 4)
- Updating CLAUDE.md files (step 5)

## Instructions

### 1. Identify Modified Code

Find all files that were modified during this session. Focus on:
- New functions/methods added
- Modified logic in existing functions
- New data structures or classes
- Integration points where new code connects to existing systems

### 2. Review for Bugs

Check each modified section for common issues:
- **Null/invalid pointer access** - Are pointers dereferenced safely?
- **Array bounds** - Are all array/vector accesses within valid ranges?
- **Uninitialized variables** - Are all variables initialized before use?
- **Resource leaks** - Are resources (memory, handles) properly managed via RAII?
- **Logic errors** - Does the control flow match the intended behavior?
- **Type mismatches** - Are conversions between types correct?
- **Math errors** - Are calculations correct (especially floating point)?

### 3. Verify Implementation Completeness

Answer these questions:
- **Do the changes implement the user's request?** Does the implementation fully address what was asked?
- **Are these the minimal changes to solve the problem?** No unnecessary additions, refactoring, or features beyond what was requested?
- **Were all required integration points updated?** Are there edge cases or scenarios not handled?

### 4. Check for Code Simplification

Answer these questions:
- **Can the code be simplified or cleaned up?**
  - Are there temporary variables used only once?
  - Are there one-line functions that can be removed? (Pass-through functions)
- **Is there any duplicated code that can be refactored into functions?**
  - Can repeated logic be extracted?
- **Are there helper functions in `/Common/` that could be used?**
  - Check `/Common/Utils.h` for general utilities
  - Check `/Common/MathUtils.h` for math operations
  - Check other relevant Common headers

### 5. Verify Minimal Changes Principle

Ensure the changes follow the minimal changes philosophy:
- No unnecessary refactoring of surrounding code
- No additional features beyond what was requested
- No extra error handling or validation (per project directives)
- No cosmetic changes to unrelated code

## Output Format

Provide a structured review report:

```
## Code Review Results

### Files Reviewed
- [list of modified files]

### Bugs Found
[List any bugs discovered, or state "No bugs found"]
- file:line - Description of bug and suggested fix

### Implementation Assessment
[Evaluate completeness vs user's request]
- ✓ or ✗ for each requirement
- Note any missing functionality

### Simplification Opportunities
[List any refactoring or simplification suggestions]
- Duplicated code locations
- Available Common helper functions
- Over-complicated logic

### Minimal Changes Check
[Verify adherence to minimal changes principle]
- ✓ Changes are minimal
- or: List unnecessary additions that should be removed

### Recommendation
[PASS / NEEDS FIXES] with brief summary
```

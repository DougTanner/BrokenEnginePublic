---
name: systematic-issue-check
description: After a bug fix, evaluates whether the fix is a band-aid and whether an architectural refactor or different flow would be a better solution. Use this skill after making bug fix code changes as part of the C++ code change workflow (step 5).
allowed-tools: [Read, Grep, Glob, Task]
---

# Systematic Issue Check

Evaluates whether a bug fix addresses the root cause or merely patches a symptom, and suggests architectural alternatives if warranted.

## Instructions

### 1. Extract the Fix from Conversation History

Review the conversation to identify:
- **What the bug was** - the symptom the user observed
- **What caused it** - the underlying reason for the symptom
- **What the fix changed** - the specific code modifications applied

### 2. Evaluate Fix Depth

Assess whether the fix addresses the root cause or patches a symptom. Consider these warning signs of a band-aid fix:

- **Adds a special case or workaround** - Does the fix introduce an `if` guard, null check, or early return that works around a deeper issue?
- **Fixes one instance of a recurring problem** - Could the same bug occur in sibling code paths, other collections, or similar systems?
- **Relies on ordering or timing** - Does the fix depend on functions being called in a specific order that isn't enforced by the architecture?
- **Works around a design limitation** - Is the fix compensating for a missing abstraction, wrong data flow, or misplaced responsibility?

### 3. Consider Architectural Alternatives

If the fix appears to be a band-aid, consider what a deeper solution would look like:
- A different data flow that eliminates the problematic state
- Moving responsibility to a different system or phase
- A structural refactor that prevents the entire class of bug
- An enforced ordering or lifecycle that makes the bug impossible

### 4. Search for Related Fragility

Use Grep and Read to examine nearby code for the same fragile pattern:
- Check sibling code paths that follow the same structure
- Look for similar workarounds already present (suggesting a recurring problem)
- Check if the pattern exists in other collections or systems

### 5. Report Findings

Output a structured report. Do NOT auto-fix - report findings to the user for their decision.

```
## Systematic Issue Check

### Bug Summary
[1-2 sentences: what the bug was and what caused it]

### Fix Applied
[1-2 sentences: what the fix changed]

### Assessment: [SOLID FIX / BAND-AID]

### Reasoning
[Why the fix is solid or why it's a band-aid, referencing the criteria from step 2]

### Related Fragility
[Any similar patterns found in nearby code, or "None found"]

### Architectural Alternative (if BAND-AID)
[Description of a deeper solution that would prevent the class of bug, or omit this section if SOLID FIX]
```

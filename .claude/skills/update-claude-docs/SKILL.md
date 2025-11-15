---
name: update-claude-docs
description: Updates CLAUDE.md documentation files in directories where code was modified during the session. Use this skill after making code changes as part of the C++ code change workflow (step 5).
allowed-tools: [Read, Edit, Write, Grep, Glob]
---

# Update CLAUDE.md Documentation

Updates CLAUDE.md files in directories where code has been modified during this conversation session to keep documentation synchronized with the current codebase state.

## When to Use

Invoke this skill after making C++ code changes, as part of step 5 in the C++ Code Change Process defined in CLAUDE.md. This ensures documentation reflects the current state of the code.

## Instructions

1. **Identify affected directories**: Determine which directories contain files that were modified during this conversation session

2. **Update CLAUDE.md files**: For each directory with modified code, update or create the CLAUDE.md file to answer these questions:

   **What is in the files in this directory?**
   - A concise overview of classes and functions that are available, and their purpose

   **Why are things done the way they are?**
   - Architectural or performance reasons behind the code

3. **Content guidelines**:
   - **DO NOT mention changes or fixes, or reference what was previously there**
     - **The CLAUDE.md file should ONLY reflect what is CURRENTLY in the code**
   - **Keep descriptions concise and focused on what Claude needs to understand the codebase**
   - **DO NOT document individual variables, members, constants, or parameters**
     - Bad: "`bGamepad` - True if gamepad mode active"
     - Bad: "`kfGamepadThreshold = 0.1f` - Thumbstick deadzone"
     - Bad: "Position tracking: `pVecPositions[]`, `pVecVelocities[]`"
     - Good: "Contains gamepad state, mouse position, and menu action flags"
   - **Focus on purpose and architecture, not implementation details**
     - Document what classes/systems DO, not what members they HAVE
     - Explain design patterns and relationships between components
     - Describe data flow and system interactions
     - Mention key algorithms or architectural decisions
   - **Avoid exhaustive listings**
     - Don't list every struct member, enum value, or constant
     - Don't enumerate all flags or configuration values
     - Don't provide member-by-member breakdowns
     - Instead, summarize the category/purpose of related members
   - **Keep it high-level**
     - Readers should understand system architecture and responsibilities
     - They should NOT be able to reconstruct class definitions from the docs
     - If you're listing variable names with explanations, you're too detailed

4. **Auto-update**: Make the documentation updates directly - do not ask for permission

## Important Notes

- Don't include details about source code files in subdirectories, but do add a reference to their CLAUDE.md ex in /Source/CLAUDE.md:
	### `/Audio/` - 3D Spatial Audio
	XAudio2-based spatial audio system with voice pooling and lazy loading.
	- [Audio/CLAUDE.md](Audio/CLAUDE.md)
- Only update CLAUDE.md files in directories where code was actually modified
- Maintain consistency with existing CLAUDE.md style and format
- Documentation should help Claude understand the codebase, not serve as detailed API reference

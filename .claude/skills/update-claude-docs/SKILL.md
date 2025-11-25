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

1. **Identify affected directories**: Look at files modified in this conversation. Update CLAUDE.md only in the immediate directories containing those files (not parent directories unless their content directly changed).

2. **Create or update CLAUDE.md**:
   - If no CLAUDE.md exists in an affected directory, create one
   - If one exists, update only the sections affected by the code changes
   - The CLAUDE.md file should ONLY reflect what is CURRENTLY in the code
   - DO NOT mention changes, fixes, or reference what was previously there

3. **Target structure** (adapt sections as needed):
   ```markdown
   # [Directory Name] - [One-line Purpose]

   ## Overview
   [2-3 sentences on what this code does and why it exists]

   ## Key Classes/Systems
   - **ClassName** - What it does and its role in the system

   ## Architecture Notes
   [Design decisions, patterns used, data flow between components]

   ## See Also
   - [Subdirectory/CLAUDE.md](Subdirectory/CLAUDE.md) - Brief description
   ```

4. **Length target**: Aim for 20-50 lines per CLAUDE.md. If longer, you're likely too detailed.

5. **Auto-update**: Make the documentation updates directly - do not ask for permission

## Content Guidelines

### DO: Focus on Purpose and Architecture
- Document what classes/systems DO, not what members they HAVE
- Explain design patterns and relationships between components
- Describe data flow and system interactions
- Mention key algorithms or architectural decisions

**Good examples:**
- "Manages GPU buffer allocation with automatic memory pooling and defragmentation"
- "Implements observer pattern for decoupled event propagation between game systems"
- "Uses SOA layout for cache-efficient iteration over thousands of entities"

### DON'T: Document Implementation Details
- Don't list individual variables, members, constants, or parameters
- Don't enumerate every struct member, enum value, or flag
- Don't provide member-by-member breakdowns

**Bad examples:**
- "`bGamepad` - True if gamepad mode active"
- "`kfGamepadThreshold = 0.1f` - Thumbstick deadzone"
- "Position tracking: `pVecPositions[]`, `pVecVelocities[]`"

**Good alternative:**
- "Contains gamepad state, mouse position, and menu action flags"

### Keep it High-Level
- Readers should understand system architecture and responsibilities
- They should NOT be able to reconstruct class definitions from the docs
- If you're listing variable names with explanations, you're too detailed

## Important Notes

- **Context window awareness**: These files will be added to an LLM's finite context window. Balance clarity against conciseness.
- **No duplication**: DO NOT repeat details from CLAUDE.md files in parent directories
- **Cross-references**: DO reference child directory CLAUDE.md files with links when relevant
- **Consistency**: Maintain consistency with existing CLAUDE.md style in the codebase
- **Purpose**: Help Claude quickly understand the codebase, not serve as an API reference

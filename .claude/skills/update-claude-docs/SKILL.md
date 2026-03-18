---
name: update-claude-docs
description: Updates CLAUDE.md documentation files in directories where code was modified during the session. Use this skill after making code changes as part of the C++ code change workflow (step 8).
allowed-tools: [Read, Edit, Write, Grep, Glob]
---

# Update CLAUDE.md Documentation

Updates CLAUDE.md files in directories where code has been modified to keep documentation synchronized with the current codebase state. The user may also request a manual run to update/improve/sync CLAUDE.md files broadly — in that case investigate all relevant files.

## Instructions

1. **Identify affected directories**: Determine which directories contain modified files. If invoked as a subagent, the caller should provide the list of changed files. Otherwise, check which files were modified in this conversation. Update CLAUDE.md only in the immediate directories containing those files (not parent directories unless their content directly changed).

2. **Read existing CLAUDE.md**: Before editing, read each affected directory's CLAUDE.md (if it exists) to understand what's already documented.

3. **Create or update CLAUDE.md**:
   - If one exists, update only the sections affected by the code changes
   - Only create a new CLAUDE.md if the directory represents a distinct subsystem (not for single-file utility directories)
   - The CLAUDE.md file should ONLY reflect what is CURRENTLY in the code — do not mention changes, fixes, or reference what was previously there
   - **Separately**: if the file exceeds the 20-50 line target, trim bloated sections to bring it closer to target

4. **Target structure** (adapt sections as needed):
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

5. **Length target**: Aim for 20-50 lines per CLAUDE.md. If longer, you're likely too detailed.

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
- Don't narrate method internals or call chains (e.g., "UpdateClient() calls PollAndReconcile() which calls X then Y then Z") — that's what the code is for
- Don't duplicate content from `Documents/Architecture/` — link to it instead

**Bad examples:**
- "`bGamepad` - True if gamepad mode active"
- "`kfGamepadThreshold = 0.1f` - Thumbstick deadzone"
- "Position tracking: `pVecPositions[]`, `pVecVelocities[]`"
- "`UpdateServer()` calls `PreTickNetwork()` before physics, handles quickload/replay, calls `WaitForTick()` for the server tick timer, then runs physics..."

**Good alternative:**
- "Contains gamepad state, mouse position, and menu action flags"
- "Orchestrates the server main loop (network polling, physics ticks, broadcast). See [Architecture doc](link) for detailed flow."

### Keep it High-Level

Readers should understand system architecture and responsibilities, not be able to reconstruct class definitions. If you're listing variable names with explanations, you're too detailed.

## Important Notes

- **Context window awareness**: These files are added to an LLM's finite context window. Balance clarity against conciseness.
- **No duplication**: Do not repeat details from parent CLAUDE.md files or `Documents/Architecture/` docs — link to them instead
- **Cross-references**: Reference child directory CLAUDE.md files with links when relevant
- **Consistency**: Maintain consistency with existing CLAUDE.md style in the codebase

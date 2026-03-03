---
name: update-architecture-diagrams
description: Updates architecture diagrams in Documents/Architecture/ when code changes affect diagrammed subsystems. Use this skill after making code changes as part of the C++ code change workflow.
allowed-tools: [Read, Edit, Write, Grep, Glob, Task]
---

# Update Architecture Diagrams

Updates Mermaid architecture diagrams in `Documents/Architecture/` to stay synchronized with code changes made during this conversation.

## When to Use

- Invoke this skill after making C++ code changes, alongside `update-claude-docs`
- Only updates diagrams whose source directories were touched — skips unaffected diagrams

## Diagram Registry

Each diagram covers specific source directories. Only diagrams whose source directories contain modified files need review.

| Diagram | Source Directories |
|---------|--------------------|
| `SystemOverview.md` | `Engine/Source/` (manager singletons, init order in `Main.cpp`) |
| `FrameUpdatePipeline.md` | `Engine/Source/Frame/`, `Engine/Source/Main.cpp` |
| `Network.md` | `Engine/Source/Network/` |
| `GraphicsPipeline.md` | `Engine/Source/Graphics/`, `Engine/Source/Graphics/Managers/` |
| `GameReconciliation.md` | `Projects/BrokenEngineSandbox/Source/Game.h`, `Projects/BrokenEngineSandbox/Source/Game.cpp`, `Engine/Source/Main.cpp`, `Engine/Source/Network/NetworkClient.h` |

## Instructions

### 1. Identify Affected Diagrams

Look at the files modified in this conversation. For each diagram in the registry above, check if any modified file falls within its source directories. Collect the list of affected diagrams.

If no diagrams are affected, report "No architecture diagrams affected by these changes" and stop.

### 2. Review Each Affected Diagram

For each affected diagram, launch an Explore subagent (via Task tool) that:

1. Reads the current diagram file (`Documents/Architecture/<Name>.md`)
2. Reads the modified source files that fall within that diagram's source directories
3. Compares the diagram content against the current code and identifies any inaccuracies:
   - Manager names, initialization order, or dependencies that changed
   - Submission phases, synchronization primitives, or semaphore chains that changed
   - State machine states, transitions, or function names that changed
   - Data structures, fields, or flow that changed
4. Reports back with specific changes needed (or "no changes needed")

Launch multiple subagents in parallel if multiple diagrams are affected.

### 3. Apply Updates

For each diagram that needs changes:
- Use Edit to update only the affected sections of the diagram
- Preserve the existing diagram structure and style conventions
- Keep Mermaid syntax valid
- Use real code names (class names, function names, enum values)

### 4. Report

List which diagrams were checked and whether each was updated or unchanged.

## Important Notes

- **Minimal changes**: Only update what actually changed in the code. Don't rewrite diagrams for cosmetic reasons.
- **Preserve style**: Match the existing Mermaid styling conventions (classDef colors, node naming, subgraph structure).
- **No new diagrams**: This skill only updates existing diagrams. To create new diagrams, use `/generate-architecture-diagram`.
- **Auto-update**: Make updates directly — do not ask for permission.

---
name: update-architecture-diagrams
description: Updates architecture diagrams in Documents/Architecture/ when code changes affect diagrammed subsystems. Use this skill after making code changes as part of the C++ code change workflow.
allowed-tools: [Read, Edit]
---

# Update Architecture Diagrams

Updates a specific Mermaid diagram in `Documents/Architecture/` to reflect code changes. This skill is invoked with a specific diagram file path — the calling CLAUDE.md already knows which diagram is relevant.

## Instructions

1. **Read the diagram** specified in the invocation arguments.

2. **Check if the diagram needs updating.** Read the modified source files and compare against what the diagram shows. Most code changes (adding a field, tweaking logic) don't affect diagrams — only structural changes do (renamed classes, new phases, changed init order, new dependencies). If nothing structural changed, report "no update needed" and stop.

3. **Update only what changed.** Use Edit to fix specific inaccuracies. Preserve existing Mermaid styling conventions (classDef colors, node naming, subgraph structure). Keep changes minimal — don't rewrite for cosmetic reasons.

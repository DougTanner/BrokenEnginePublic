---
name: update-architecture-diagrams
description: Updates existing Mermaid architecture diagrams in `Documents/Architecture/` when code changes affect diagrammed subsystems. Invoke ONLY when modified files match a subsystem covered by one of the existing diagrams (currently `FrameUpdatePipeline.md`, `GameReconciliation.md`, `Network.md`). For creating a new diagram from scratch, use `/generate-architecture-diagram` instead.
allowed-tools: [Read, Edit, Glob, Grep]
---

# Update Architecture Diagrams

Updates a specific Mermaid diagram in `Documents/Architecture/` to reflect code changes. Sister skill to `/generate-architecture-diagram` — that one creates new diagrams, this one edits existing ones. Diagrams are usually discovered via the "See also" link in the nearest subsystem CLAUDE.md.

## Instructions

1. **Resolve the target diagram.** If the caller passed a specific diagram path, use it. Otherwise, Glob `Documents/Architecture/*.md` and match the modified source files against each diagram's subsystem (read each diagram's first 20 lines to learn its scope). If no diagram matches the modified files, report "no diagram affected" and stop.

2. **Read the diagram** specified in the invocation arguments (or discovered in step 1).

3. **Check if the diagram needs updating.** Read the modified source files and compare against what the diagram shows. Most code changes (adding a field, tweaking logic) don't affect diagrams — only structural changes do (renamed classes, new phases, changed init order, new dependencies). If nothing structural changed, report "no update needed" and stop.

4. **Update only what changed.** Use Edit to fix specific inaccuracies. Preserve existing Mermaid styling conventions (classDef colors, node naming, subgraph structure). Keep changes minimal — don't rewrite for cosmetic reasons.

5. **Drift check.** If node names or structural labels changed, Grep the nearest subsystem CLAUDE.md for references to the old names. If the prose there has drifted, flag it to the user (do not silently rewrite CLAUDE.md — that's `/update-claude-docs`' job).

---
name: update-architecture-diagrams
description: Updates existing Mermaid architecture diagrams in `Documents/Architecture/` when code changes affect a diagrammed subsystem — e.g. frame update phases (`FrameUpdatePipeline.md`), client reconciliation/rollback (`GameReconciliation.md`), network protocol (`Network.md`); the directory is the source of truth as diagrams are added. Invoke only when modified files fall under a subsystem one of those diagrams covers. For creating a new diagram from scratch, use `/generate-architecture-diagram` instead.
allowed-tools: [Read, Edit, Glob, Grep]
---

# Update Architecture Diagrams

Updates a specific Mermaid diagram in `Documents/Architecture/` to reflect code changes. Sister skill to `/generate-architecture-diagram` — that one creates new diagrams, this one edits existing ones.

## Instructions

1. **Resolve the target diagram.** If the caller passed a specific diagram path, use it. Otherwise check the nearest AGENTS.md above the modified source files for a "See also" link into `Documents/Architecture/` — that link is how `/generate-architecture-diagram` registers a diagram's subsystem. If no link, Glob `Documents/Architecture/**/*.md` and match the modified files against each diagram's scope (the `> Auto-generated ... from <path>` header line names its source directories). If no diagram matches, report "no diagram affected" and stop.

2. **Check if the diagram needs updating.** Read the diagram and the modified source files, then compare. Most code changes (adding a field, tweaking logic) don't affect diagrams — only structural changes do (renamed classes, new phases, changed init order, new dependencies). If nothing structural changed, report "no update needed" and stop.

3. **Update only what changed.** Use Edit to fix specific inaccuracies. Preserve existing Mermaid styling conventions (classDef colors, node naming, subgraph structure). Keep changes minimal — don't rewrite for cosmetic reasons.

4. **Drift check.** If node names or structural labels changed, Grep the nearest subsystem AGENTS.md for references to the old names. If the prose there has drifted, flag it to the user (do not silently rewrite AGENTS.md — that's `/update-claude-docs`' job).

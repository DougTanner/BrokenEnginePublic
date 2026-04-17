---
name: generate-architecture-diagram
description: Generates Mermaid architecture diagrams for cross-system relationships. Only creates diagrams spanning 3+ files. Output linked from subsystem CLAUDE.md files. Only invoke when the user explicitly requests it (e.g., "/generate-architecture-diagram", "generate an architecture diagram") or when another skill explicitly instructs it. Never trigger autonomously from general code questions or during routine code changes.
disable-model-invocation: true
allowed-tools: [Read, Write, Edit, Glob, Grep, Agent]
---

# Generate Architecture Diagram

Generates Mermaid-based architecture diagrams for cross-system relationships in a C++23 data-oriented Vulkan game engine.

## Arguments

The user provides a target path (file or directory). If no path is given, ask for one.

## Gate: Should This Diagram Exist?

Before doing any work, answer these questions. If the answer to ANY is "no", **tell the user why and stop** — do not create the diagram:

1. Does the target involve **3+ source files** with non-obvious relationships between them?
2. Would an AI agent need **more than 2 grep/read operations** to understand the relationships?
3. Is this information **not already captured** in the nearest CLAUDE.md file?
4. Does the diagram show a **complex state machine, synchronization chain, or multi-phase pipeline** — not just "A owns B, B owns C"?
5. Is this a **cross-system** relationship (spanning multiple directories/namespaces), not single-subsystem internals?

If the relationships can be described in 2-3 sentences of prose, **add them to the subsystem's CLAUDE.md instead**. Prefer enriching CLAUDE.md over creating a new diagram — CLAUDE.md files are always read by agents, diagrams are not.

**The bar is high.** The codebase currently has 3 diagrams: `Documents/Architecture/GameReconciliation.md`, `Documents/Architecture/FrameUpdatePipeline.md`, and `Documents/Architecture/Network.md`. A new diagram must be as valuable as these (reconciliation state machines, frame phase ordering, network protocol). Most subsystems do NOT need a diagram.

**Sister skill:** Once a diagram exists, subsequent code changes that affect it are updated by `/update-architecture-diagrams` — not by re-running this skill.

## What Makes a Useful Diagram

Useful diagrams show things that are **hard to derive from reading code**:
- **Initialization/destruction order** spanning many files (e.g., 11 Vulkan managers in strict sequence)
- **Synchronization chains** (semaphores, fences, mutexes across threads/submissions)
- **Temporal phase ordering** across multiple systems (frame update phases, pipeline stages)
- **Protocol state machines** spanning client/server boundaries
- **Data flow across system boundaries** (not within a single class)

## What Does NOT Belong in a Diagram

- **Single-subsystem internals** — if it's all in one directory, it doesn't need a diagram. Put it in the CLAUDE.md
- **Anything the subsystem's CLAUDE.md already describes** — read the nearest CLAUDE.md FIRST and check for redundancy
- **Simple ownership/dependency trees** — "A owns B, B uses C" is prose, not a diagram. Diagrams are for relationships too complex for prose
- **Field listings, enum values, struct members, parameter lists** — these restate code
- **Code-restating flowcharts** — walking through a function's implementation line-by-line
- **Caller/callee lists** without data flow context
- **`classDiagram` type** — encourages field/method listings
- **Manager init order lists** — unless the ordering involves complex dependencies with conditional branches (a simple linear sequence is better as a bullet list in CLAUDE.md)

## Diagram Types

| Type | Syntax | When to Use |
|------|--------|-------------|
| Dependency graph | `graph TD` | Cross-system ownership and dependency |
| Data flow | `flowchart LR` | Pipeline-style data movement across boundaries |
| Sequence diagram | `sequenceDiagram` | Temporal flows with synchronization (GPU submission, network protocol) |

## Content Rules

- **Nodes** = systems, managers, classes — not fields, enums, or variables
- **Edge labels** = 1-3 words (owns, signals, reads, waits) — not function signatures
- **Max ~30 nodes per diagram**, max ~4 diagrams per file
- **1-2 sentences of prose per diagram** — not a paragraph
- Every node must have at least one edge
- Use `classDef` for client/server coloring: blue = client-only, red = server-only, gray = shared

```mermaid
%%{init: {'theme': 'default'}}%%
graph TD
    classDef clientOnly fill:#dbeafe,stroke:#3b82f6
    classDef serverOnly fill:#fee2e2,stroke:#ef4444
    classDef shared fill:#f3f4f6,stroke:#6b7280
```

## Instructions

### 1. Explore the Target

Use the Agent tool to launch an Explore subagent analyzing the target path. Prompt it to report:
- Cross-system dependencies (what external managers/globals does this area use?)
- `gp*` global pointer relationships
- `#ifdef BT_CLIENT`/`BT_SERVER` boundaries
- Synchronization primitives (semaphores, fences, mutexes, worker dispatches)
- Temporal ordering (init order, per-frame phase order)
- Data flow across system boundaries

### 2. Generate Diagrams

Synthesize findings into diagrams. **Before adding any node, ask: does this show a cross-system relationship, or does it restate what one source file says?** If it restates code, do not include it.

### 3. Self-Review

Before writing, check each diagram:
1. Does every diagram show relationships spanning **3+ source files**?
2. Are there any field listings, enum catalogs, or parameter lists? **Remove them.**
3. Could any two diagrams be merged? **Merge them.**
4. Would 2-3 sentences in a CLAUDE.md replace this diagram? **Don't create it.**

### 4. Write Output and Link

Write to `Documents/Architecture/<Area>/<Name>.md`:

```markdown
# Architecture: [Name]

> Auto-generated by `/generate-architecture-diagram` from `[target path]`

## [Section Title]

[1-2 sentence explanation]

```mermaid
[diagram]
```
```

Then add a "See also" link to the diagram from the **nearest CLAUDE.md** to the source code (not the root CLAUDE.md). This is critical for discoverability — an AI agent will find the diagram by reading the subsystem's CLAUDE.md, not by browsing `Documents/Architecture/`.

This link is also how `/update-architecture-diagrams` discovers which diagrams belong to which subsystem when future code changes land. Skipping the link leaves the diagram orphaned.

### 5. Report

- Which diagrams were generated and why they meet the 3+ file cross-system threshold
- The output file path
- Which CLAUDE.md was updated with the link

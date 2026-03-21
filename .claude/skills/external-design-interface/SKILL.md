---
name: external-design-interface
description: >-
  Generate multiple radically different C++ interface designs for an engine system
  using parallel sub-agents, then compare and synthesize. Use when user explicitly
  requests it (e.g., "/external-design-interface", "design this API", "explore interface options",
  "design it twice"). Also proactively suggest when detecting the user is designing a new
  system — especially new Collections (game or engine), new manager classes, or new
  subsystem APIs. When auto-detecting, ask the user "Would you like me to run
  /external-design-interface to explore different API shapes?" before invoking.
allowed-tools: [Read, Grep, Glob, Agent]
---

# Design Interface

Generate multiple radically different C++ interface designs for an engine system using parallel sub-agents, then compare and synthesize the best approach.

## Arguments

The user provides a description of the system to design. If no description is given, ask what system they want to explore interface options for.

## Instructions

### 1. Gather Requirements

Before designing, understand:
- What problem does this system solve?
- Who are the callers? (other managers, collections, frame phases, game code)
- What are the key operations?
- Constraints: allocation tracking, client/server builds, SOA layout, frame phase boundaries, determinism, threading

Use the codebase to answer as many of these as possible before asking the user.

### 2. Generate Designs (Parallel Sub-Agents)

Spawn 3+ sub-agents using Agent tool. Each gets a different design constraint:
- Agent 1: "Minimize the interface — 1-3 entry points max, opaque internals"
- Agent 2: "Maximize SOA data locality — optimize for cache-friendly iteration and workbuffer usage"
- Agent 3: "Optimize for the most common caller pattern in this codebase"
- Agent 4 (if applicable): "Design around the existing manager singleton pattern (`gp*` globals)"

Each agent outputs:
1. Interface signature (types, methods, params) — C++ code
2. Usage example showing how callers use it
3. What complexity it hides internally
4. Client/server build impact (`#ifdef BT_CLIENT` surface area)
5. Memory/allocation implications (workbuffer vs heap)
6. Trade-offs

### 3. Present Designs

Show each design with interface signature, usage examples, what it hides. Present sequentially.

### 4. Compare Designs

Compare on:
- **Interface simplicity**: fewer methods, simpler params
- **Depth**: small interface hiding significant complexity (deep module = good)
- **SOA friendliness**: does the shape work well with data-oriented design?
- **Client/server code gating**: how much `#ifdef` surface area?
- **Allocation overhead**: heap allocations in main loop?
- **Thread safety**: safe under `gpMultithreading->Dispatch()`?
- **Frame phase clarity**: clean Update vs PostRender separation?

Discuss trade-offs in prose. Give opinionated recommendation.

### 5. Synthesize

Ask which design best fits, whether elements from others are worth incorporating.

### Anti-Patterns
- Don't let sub-agents produce similar designs — enforce radical difference
- Don't skip comparison — the value is in contrast
- Don't implement — this is purely about interface shape
- Don't evaluate based on implementation effort

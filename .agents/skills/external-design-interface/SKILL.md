---
name: external-design-interface
description: >-
  Generate multiple radically different C++ interface designs for an engine system
  using parallel sub-agents, then compare and synthesize the best approach. When
  auto-detecting (not explicitly requested), ask the user "Would you like me to run
  /external-design-interface to explore different API shapes?" before invoking.
when_to_use: >-
  When the user explicitly requests it ("design this API", "explore interface
  options", "design it twice"), or proactively when detecting the user is designing
  a new system — especially new Collections (game or engine), new manager classes,
  or new subsystem APIs. Do NOT auto-suggest for bug fixes, single-function
  additions, or adding a member to an existing collection — those route to
  `/add-collection-member`.
allowed-tools: [Read, Grep, Glob, Agent, AskUserQuestion]
---

# Design Interface

## Arguments

The user provides a description of the system to design. If none is given, use AskUserQuestion to ask what system to explore interface options for, and wait for a response before proceeding.

## Instructions

### 1. Gather Requirements

Before designing, understand:
- What problem does this system solve?
- Who are the callers? (other managers, collections, frame phases, game code)
- What are the key operations?
- Constraints: allocation tracking, client/server builds, SOA layout, frame phase boundaries, determinism, threading

Use the codebase to answer as many of these as possible before asking the user.

### 2. Explore Existing Patterns

Launch an Explore agent (`subagent_type: "Explore"`, `model: "opus"`) to find:
- Similar systems in the codebase (same problem domain or similar shape)
- The dominant caller pattern for this kind of system (how do existing callers invoke similar APIs?)
- Relevant conventions (naming, parameter ordering, `gp*` usage, workbuffer patterns)

Record these findings — they get passed to every design agent in the next step.

### 3. Generate Designs (Parallel Sub-Agents)

Spawn exactly 3 Plan agents (`subagent_type: "Plan"`, `model: "fable"`) in parallel using the Agent tool. The `Plan` and `Explore` subagent types are custom agents defined in this environment — if they are unavailable, fall back to `subagent_type: "general-purpose"` with the role ("plan designer" / "explorer") embedded at the top of the prompt. Each agent's prompt must include:
- The requirements gathered in step 1
- The existing patterns and caller conventions found in step 2
- One of the design constraints below

**Design constraints** (each targets a fundamentally different optimization axis):
- **Agent 1 — Minimal surface area**: "Design the interface with 1-3 entry points max, hiding all complexity behind opaque internals. Favor a deep module — small interface, significant internal machinery."
- **Agent 2 — Data locality**: "Design the interface to maximize SOA data locality — optimize for cache-friendly iteration, workbuffer usage, and batch processing of contiguous arrays."
- **Agent 3 — Caller ergonomics**: "Design the interface to be ergonomic for [specific caller pattern found in step 2], prioritizing call-site simplicity and readability."

**Conditional 4th agent**: Add a 4th agent only when the system being designed is a manager or singleton: "Design around the existing `gp*` singleton pattern, following how other managers expose their API."

Each agent outputs:
1. Interface signature (types, methods, params) — C++ code
2. Usage example showing how callers use it
3. What complexity it hides internally
4. Client/server build impact (`#ifdef BT_CLIENT` surface area)
5. Memory/allocation implications (workbuffer vs heap)
6. Trade-offs

### 4. Present & Compare

Show each design sequentially with interface signature, usage examples, and what it hides.

Then compare on:
- **Interface simplicity**: fewer methods, simpler params
- **Depth**: small interface hiding significant complexity (deep module = good)
- **SOA friendliness**: does the shape work well with data-oriented design?
- **Client/server code gating**: how much `#ifdef` surface area?
- **Allocation overhead**: heap allocations in main loop?
- **Thread safety**: safe under `gpMultithreading->Dispatch()`?
- **Frame phase clarity**: clean Update vs PostRender separation?

Discuss trade-offs in prose. Give an opinionated recommendation.

### 5. Synthesize

Ask which design best fits, and whether elements from other designs are worth incorporating.

After the user picks, output a final synthesized interface:
1. A C++ header-style code block with the complete interface (types, methods, params)
2. Brief usage examples at key call sites
3. Notes on any elements incorporated from other designs

### 6. Handoff

Suggest running `/external-grill-plan` next to resolve determinism / client-server / memory / threading / frame-phase decisions on the synthesized design before implementation. For new Collection systems, also remind the caller that `/add-collection` owns the mechanical wiring steps once the shape is fixed.

### Anti-Patterns
- Each agent prompt specifies a fundamentally different optimization axis — do not soften or merge the constraints, as the value comes from contrast between divergent designs
- Do not skip the comparison step — presenting designs without contrasting them loses most of the skill's value
- Do not implement beyond interface shape — no .cpp bodies, no allocation code, just the API surface
- Do not evaluate designs based on implementation effort — focus on the quality of the interface for callers

---
name: external-grill-plan
description: >-
  Interview the user about a loaded plan to resolve ambiguities and gather missing
  information before implementation begins. Use this skill after a plan is loaded as
  part of the C++ code change workflow (step 0.5). Walks each branch of the decision
  tree, asking engine-specific questions about determinism, client/server, memory,
  threading, and frame phases. For each question, provides a recommended answer based
  on codebase exploration.
allowed-tools: [Read, Grep, Glob, Agent]
---

# Grill Plan

Interview the user about every aspect of this plan until reaching shared understanding. Walk down each branch of the design tree, resolving dependencies between decisions one-by-one.

## Rules
- For each question, provide your recommended answer based on codebase exploration
- If a question can be answered by exploring the codebase, explore it instead of asking
- Ask one focused question at a time, not a batch of 10
- Stop when all branches of the decision tree are resolved

## Engine-Specific Interrogation Branches

Always probe these areas if the plan touches them:

- **Determinism**: Will this produce identical results on client and server? Are there floating-point or ordering dependencies?
- **Client/Server**: What happens in the server build where client-only code is stripped? Are `#ifdef BT_CLIENT` guards at the narrowest scope?
- **Memory**: Does this allocate on the heap in the main loop? Can it use `gpThreadLocal->mWorkbuffer` instead? Does it need `ScopedSuppressAllocationTracking`?
- **Threading**: Is this safe under `gpMultithreading->Dispatch()`? What's the data access pattern? Any shared mutable state?
- **Frame phases**: Which phase does this run in? Does it respect Update vs PostRender boundaries?
- **Collection integrity**: Does this maintain SOA alignment? Are all member arrays updated consistently across AllocateAndCopy, LogDifferences, Spawn, Transfer?
- **Layer compliance**: Does engine code access game-layer through `game::gpGame`? Any new cross-layer dependencies?

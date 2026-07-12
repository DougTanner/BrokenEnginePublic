---
name: external-grill-plan
description: >-
  Interviews the user about a loaded plan to resolve ambiguities and fill gaps
  before implementation. Invoke after a plan is loaded or created and before
  any code changes. Walks
  each decision branch with engine-specific questions (determinism,
  client/server, memory, threading, frame phases), recommending an answer for
  each from codebase exploration.
allowed-tools: [Read, Grep, Glob, Edit, Agent, AskUserQuestion]
---

# Grill Plan

Interview the user about every aspect of this plan until reaching shared understanding.

## Inputs

- Plan file and explicit user intent
- Accepted `/plan-audit` findings and repository evidence, or none for direct invocation
- Applicable repository instructions and known constraints

## Rules
- For each question, provide your recommended answer based on codebase exploration
- If a question can be answered by exploring the codebase, explore it instead of asking
- Ask one focused question at a time, not a batch of 10
- Begin with the first concrete unresolved decision; do not preface the interview with a plan summary or restatement
- Use adversarial thinking — actively try to find flaws, then provide concrete suggestions to fix them
- Skip branches that are clearly irrelevant to the plan (e.g., don't probe determinism for a client-only UI change)
- Stop when all branches of the decision tree are resolved

## Workflow
1. Read the plan file and any accepted audit findings from the current conversation context
2. Validate and incorporate supplied findings, then identify all remaining decision points, ambiguities, and unstated assumptions — scan against the Decision-Point Taxonomy below; plans routinely leave these classes implicit
3. Walk each branch of the decision tree, resolving dependencies one-by-one
4. Ask the Closing Question (below) as the final interview question
5. When all branches are resolved, silently update the plan file with the resolved details, then immediately return control to continue the calling workflow's next step — no summary, no "ready to proceed?" prompt, no stop.

## Closing Question

After all branches are resolved but before updating the plan file, ask one final question:

> "The biggest thing I think you may be missing about this situation is: \<X\>."

Derive X by zooming out from the plan. Run these prompts and present the strongest hit:

1. Which queued plan (`Documents/Plans/Order.md` or `Documents/Features/Order.md`) shares these files/symbols — does this plan's shape contradict it or invalidate its citations?
2. What adjacent system consumes the state this plan changes (collection members, manager outputs, shared headers), and does the plan account for it?
3. Is there a simpler move that makes the plan unnecessary — delete the code instead of fixing it, reuse an existing `common::` mechanism, or vendor a library?
4. What invariant surface (CRC, `kiVersion`/`.pack` layout, network protocol, save/replay format, main-loop allocation tracking) does this touch that the plan never mentions?
5. What does the plan assume about scale that the unbounded world breaks — uncapped entity counts, sparse-cell parallelism, kilometer-scale coordinate magnitudes?

If nothing qualifies, say so and skip — do not invent one. If the user's answer changes anything, fold it into the plan before the silent update.

## Decision-Point Taxonomy

Recurring ambiguity classes to scan for in Workflow step 2:

- **Unresolved option list** — the plan presents A/B/C alternatives without committing; resolve to exactly one before implementation.
- **Placement/ownership** — "add a helper/field/manager" without saying which layer, TU, or class owns it.
- **Delete-vs-reserve** — dead machinery: remove outright, or keep the enum slot / wire ID / version term reserved for compatibility.
- **Behavior change hiding in a refactor** — a "rename/unify/collapse" step that silently alters semantics (ordering, error path, a default).
- **Granularity** — per-field vs per-system gates, one split vs several, single vs split arenas/buffers.
- **Magic defaults** — sizes, thresholds, counts stated without justification, or needed but absent.
- **Undeclared invariant exposure** — the edit touches CRC'd state, `kiVersion`/`.pack` layout, protocol, save/replay format, or allocation-tracked paths, but the plan never says so.
- **Self-contradicting requirements** — two statements in the plan that cannot both hold (e.g., an "always/never" in one clause revoked by another); surface the contradiction and resolve it with the user, never pick one side silently.
- **Cross-plan contradiction** — another queued plan touches the same files/symbols with an incompatible shape (also probed by Closing Question prompt 1).

## Role Boundary
This skill fills gaps in an existing plan. **Do not re-design** the interface — that is `/external-design-interface`'s job. If the plan's interface shape is itself unclear, stop and recommend running `/external-design-interface` first.

## Existing-Library Gate (Reinventing the Wheel?)

Run this gate **first**, before any other interrogation, whenever the plan introduces or rewrites a non-trivial subsystem, algorithm, or data structure — anything where a mature open-source library plausibly already solves the problem. Examples: geometry / mesh processing, pathfinding / navmesh, physics, compression, serialization, networking transport, math primitives, image / texture codecs, audio DSP, JSON / config parsing, string formatting, ECS, scripting, profiling.

Skip the gate for: bug fixes, refactors, tuning passes, content-only changes, engine-glue work, or anything tightly bound to internal types where no external library could realistically slot in.

Steps:
1. Form a one-sentence statement of the capability being built (e.g., "polygon offsetting", "navmesh generation from triangle soup", "Reed-Solomon erasure coding").
2. Identify 1–3 candidate libraries that already implement this with a commercial-friendly license (MIT, BSD, Zlib, Apache-2.0, Boost, MPL-2.0). Reject GPL / AGPL / LGPL-static / "non-commercial" / "source-available". Delegate each library claim to a Sonnet `/verify-external-claims` subagent for official links and decisive evidence (license, last release, platform support).
3. For each candidate, note in one line: license, maturity (last release / active commits), C++ compatibility (header-only? C++23 clean? Windows MSVC builds?), and integration cost vs. the plan's hand-rolled scope.
4. Check `ThirdParty/` and `ThirdParty/Prebuilts/` — we may already vendor a library that covers this.
5. **Present the candidates to the user with a recommendation** before grilling implementation details:
   - "Use library X" (preferred if a mature commercial-friendly option exists and integration cost < hand-roll cost)
   - "Hand-roll because <specific reason>" (e.g., need deterministic cross-platform output, license incompatibility, dependency bloat, library missing critical feature)
   - "Wrap library X with thin adapter" (use upstream for the hard part, keep our API)
6. If the user picks a library, **stop grilling the hand-rolled plan** and either return control to the calling context (nothing left to implement) or pivot to a short integration plan covering: vendoring location, build wiring (`ThirdParty.vcxproj` + filters), namespace / header isolation, and which engine call sites swap over.
7. If the user confirms hand-roll, record the rejection reason in the plan file ("Considered <lib>, rejected because <reason>") and continue to the standard branches.

## Bug-Fix Pre-Step (Hypothesis Ranking)

When the loaded plan is a bug fix or regression diagnosis (filename starts with `Bugfix_`, or the plan describes a broken behavior), prepend the following to the workflow above — **before** walking the engine-specific branches:

1. From the plan's symptom description, generate **3–5 ranked falsifiable hypotheses** for the cause. Each hypothesis must state a prediction:
   > "If `<X>` is the cause, then changing `<Y>` will make the bug disappear / changing `<Z>` will make it worse."
2. If a hypothesis cannot be stated as a prediction, it's a vibe — discard or sharpen it.
3. **Present the ranked list to the user before grilling implementation details.** The user often has context that instantly re-ranks ("we just changed #3 yesterday") or rules out hypotheses already disproven. Cheap checkpoint, big time saver.
4. Once the user confirms or re-ranks, proceed to the standard engine-specific interrogation branches with the leading hypothesis as the working assumption.

Skip this pre-step for refactor / debt / capability plans — those have no "cause" to hypothesize about; the standard interrogation branches cover them directly.

## Engine-Specific Interrogation Branches

Always probe these areas if the plan touches them:

- **Determinism**: Will this produce identical results on client and server? Are there floating-point or ordering dependencies?
  _e.g., "The plan sorts entities by distance — is the sort stable, or could client/server diverge on ties?"_
- **Client/Server**: What happens in the server build where client-only code is stripped? Are `#ifdef BT_CLIENT` guards at the narrowest scope?
  _e.g., "This new field is only used for rendering. Should it live in `ClientMembers()` with a `BT_CLIENT` guard?"_
- **Memory**: Does this allocate on the heap in the main loop? Can it use `gpThreadLocal->mWorkbuffer` instead? Does it need `ScopedSuppressAllocationTracking`?
  _e.g., "The plan adds a `std::vector<EntityId>` in `Update()`. This would trigger the allocation tracker — should it use workbuffer instead?"_
- **Threading**: Is this safe under `gpMultithreading->Dispatch()`? What's the data access pattern? Any shared mutable state?
  _e.g., "This writes to a shared counter during `Dispatch()`. Should it use per-thread accumulators and reduce after?"_
- **Frame phases**: Which phase does this run in? Does it respect Update vs PostRender boundaries?
  _e.g., "The plan reads position data during Update, but positions aren't finalized until PostRender. Should this move to PostRender?"_
- **Collection integrity**: Does this maintain SOA alignment? Are all member arrays updated consistently across AllocateAndCopy, LogDifferences, Spawn, Transfer?
  _e.g., "You're adding a new member to Blasters — have you accounted for it in AllocateAndCopy and LogDifferences?"_
- **CRC / LogDifferences**: Does the new member participate in CRC (i.e., lives in `SharedMembers()` or `SharedCrcMembers()`)? Does it need a `LogDifferences` entry for desync debugging?
  _e.g., "This field lives in SharedMembers — it will be CRC'd. Is that intended? If yes, does LogDifferences need to log it?"_
- **Interpolation vs snap**: Does this field need between-frame interpolation (PostRender+Interpolate paired collection), or does it snap on state changes?
  _e.g., "Position interpolates, but this new ID field doesn't — should it live in PostRender only?"_
- **Build wiring**: Any new files? Which vcxproj filters get updated (client AND server for game collections — four files total)? Any new `#include <std>` that should move to `Common/ExternalHeaders.h`?
- **Layer compliance**: Does engine code access game-layer through `game::gpGame`? Any new cross-layer dependencies?
  _e.g., "This engine code references a game-specific enum directly. Should it go through `game::gpGame` instead?"_

## Completion Report

After all decisions are resolved, give this handoff to the calling context
without ending the user turn; the caller continues directly into implementation:

```text
Files changed:
- <plan path, or none>
Functions/regions touched:
- <plan section changed, or none>
Residuals:
- <unresolved decision or none>
```

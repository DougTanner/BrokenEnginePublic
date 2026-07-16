---
name: external-grill-plan
description: >-
  Interviews the user about a Tier-3 plan to resolve architectural ambiguities
  before implementation. Do not invoke for Tier-1 or Tier-2 task-driven work.
  Walks
  each decision branch with engine-specific questions (determinism,
  client/server, memory, threading, frame phases), recommending an answer for
  each from codebase exploration.
allowed-tools: [Read, Write, Grep, Glob, Edit, Agent, PowerShell, AskUserQuestion]
---

# Grill Plan

Use this skill only when a Tier-3 execution card has a material unresolved
decision that repository inspection cannot settle. Interview the user about
only that decision until reaching shared understanding.

## Inputs

- Plan file and explicit user intent
- Inline `/plan-audit` findings and accepted decisions, or none for direct invocation
- Applicable repository instructions and known constraints
- Approval state: `not-approved` or `approved`, plus any previously approved delta summary
- Draft manager execution-control record, when available: fixed process baseline,
  proposed risk tier and triggers, required and conditional roles, and the initial
  acceptance-criterion matrix

## Rules
- For each question, provide your recommended answer based on codebase exploration
- If a question can be answered by exploring the codebase, explore it instead of asking
- Batch up to three independent decision points into one interaction; keep only genuinely dependent follow-ups for a later batch
- Begin with the first concrete unresolved decision; do not preface the interview with a plan summary or restatement
- Use adversarial thinking — actively try to find flaws, then provide concrete suggestions to fix them
- Skip branches that are clearly irrelevant to the plan (e.g., don't probe determinism for a client-only UI change)
- Stop when all branches of the decision tree are resolved

## Workflow
1. Read the plan file and any accepted audit findings from the current conversation context
2. Validate and incorporate supplied findings, then identify only remaining material decision points that repository inspection cannot settle — including the risk classification, role triggers, and initial acceptance matrix — and scan them against the Decision-Point Taxonomy below. Do not turn optional improvements or possible future needs into decisions.
3. Walk the decision tree in dependency order, batching up to three currently independent branches
4. Run the Internal Closing Check below
5. For a not-yet-approved plan, update the plan file with the resolved details, then immediately return control so the caller presents it for approval. For an approved plan, do not edit it: return an exact proposed delta and let main decide whether approval remains valid.

## Decision batching and UI

- Prefer one multi-question UI interaction for up to three currently independent decisions. Give each question the codebase-backed recommendation and 2–3 mutually exclusive choices.
- With Claude, use one `AskUserQuestion` call containing at most three independent questions supported by that UI.
- With Codex, use `request_user_input` when it is available, with up to three questions per call. If more than three independent decisions remain, send the minimum number of consecutive UI batches.
- When Codex `request_user_input` is unavailable (including Default mode), batch independent decisions as concise open-ended plain-text prompts without lettered or multiple-choice options. State recommendations in prose when useful. Continue with reasonable assumptions for non-blocking decisions; ask only genuinely blocking questions, and keep those concise.
- Keep dependent questions out of the current batch. Resolve their prerequisites first, then batch up to three newly unblocked questions.

## Internal Closing Check

After all branches are resolved, do not ask a routine closing question. Run these prompts internally:

1. Which queued plan (`Documents/Plans/Order.md` or `Documents/Features/Order.md`) shares these files/symbols — does this plan's shape contradict it or invalidate its citations?
2. What adjacent system consumes the state this plan changes (collection members, manager outputs, shared headers), and does the plan account for it?
3. Is there a simpler move that makes the plan unnecessary — delete the code instead of fixing it, reuse an existing `common::` mechanism, or vendor a library?
4. What invariant surface (CRC, `kiVersion`/`.pack` layout, network protocol, save/replay format, main-loop allocation tracking) does this touch that the plan never mentions?
5. What does the plan assume about scale that the unbounded world breaks — uncapped entity counts, sparse-cell parallelism, kilometer-scale coordinate magnitudes?

Surface a result only when current repository evidence establishes a concrete material decision the plan missed. Fold it into the next question batch; otherwise stop without inventing or reporting one.

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
- **Cross-plan contradiction** — another queued plan touches the same files/symbols with an incompatible shape (also probed by Internal Closing Check prompt 1).
- **Execution-control ambiguity** — the proposed tier lacks a concrete trigger,
  a role is unconditional without a matching file/risk trigger, or an acceptance
  criterion lacks a decisive check and expected result. Resolve the classification
  and matrix before approval; never lower a tier already approved by the user
  without returning that material change to main.

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
7. If the user confirms hand-roll, record the rejection reason in a not-yet-approved plan file ("Considered <lib>, rejected because <reason>") and continue to the standard branches. For an approved plan, include that exact addition in the proposed delta without editing.

## Bug-Fix Pre-Step (Internal Hypothesis Ranking)

When the loaded plan is a bug fix or regression diagnosis (filename starts with `Bugfix_`, or the plan describes a broken behavior), prepend the following to the workflow above — **before** walking the engine-specific branches:

1. From the plan's symptom description, generate **3–5 ranked falsifiable hypotheses** for the cause. Each hypothesis must state a prediction:
   > "If `<X>` is the cause, then changing `<Y>` will make the bug disappear / changing `<Z>` will make it worse."
2. If a hypothesis cannot be stated as a prediction, it's a vibe — discard or sharpen it.
3. Verify and rank the hypotheses from repository evidence, logs, and supplied diagnostics. Keep the ranking internal when the leading cause is already confirmed unambiguously and the alternatives are disproven or do not change the implementation plan.
4. Surface hypotheses to the user only when their knowledge is required to choose between two or more still-plausible causes that would materially change the fix or verification strategy. Ask the narrow unresolved factual question; do not ask the user to approve, confirm, or re-rank an analysis the repository already resolves.
5. Proceed to the standard engine-specific interrogation branches with the evidence-backed leading hypothesis as the working assumption. Record any unresolved causal ambiguity in the plan.

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

Return this complete handoff inline. Questions, recommendations, any material
decision surfaced by the internal closing check, and any material-delta
approval remain live user interaction:

```text
Plan delta: none | non-material | material
Exact proposed changes: <none, or precise plan edits; required for an approved plan>
Execution-control decisions: <risk tier/triggers, required and conditional roles,
and initial criterion -> decisive check -> expected result -> independent signal
if duplicate rows for main to record>
Files changed:
- <plan path, or none>
Functions/regions touched:
- <plan section changed, or none>
Residuals:
- <unresolved decision or none>
```

`Plan delta` describes change relative to the supplied plan state. Before initial approval, plan edits are normal refinement. After approval, `material` means behavior, acceptance criteria, scope, architecture, or verification obligations would change; only main may apply the exact delta after explicit user approval. A non-material correction cannot silently alter those dimensions.

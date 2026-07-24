<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Local Architecture Boundary Tripwires

## Context

Architecture guidance is strongest when the owning local AGENTS.md states the prohibited action and the exact condition that stops an edit. The four target documents already describe these constraints, but as descriptive prose scattered through general paragraphs:

- `Common/AGENTS.md` — the `## Overview` sentence "It is independent of Engine and game layers, but may wrap platform or third-party facilities exposed through the approved aggregation headers."
- `Engine/Source/AGENTS.md` — the `## Hub Conventions` bullet **Engine/game contract**: engine code may use required game symbols, but game-specific concepts must not enter engine-owned types; leaf-documented ownership exceptions are deliberate.
- `Engine/Source/Frame/AGENTS.md` — the first `## Architecture` bullet: `FrameInterpolateBase` owns continuous interpolation state, `FramePostRenderBase` owns committed deterministic state, and client-only visual state stays outside shared CRCs.
- `Projects/BrokenEngineSandbox/Source/Frame/AGENTS.md` — the `## Invariants` bullets stating that `RunFrameTick` is shared by server simulation and client replay and verifies the floating-point environment, and that Interpolate precedes PostRender update/collision/transfer/destroy/spawn with the shared CRC stamped after those phases.

This plan converts each constraint into a concise local tripwire — the prohibited action plus the exact stop trigger — at its single owning document, without adding root `AGENTS.md` context or inventing new architecture.

## Design

Each numbered item below is the complete specification for one edit region. Every tripwire replaces at least as much nearby descriptive prose as it adds; none appends a duplicate of prose left in place.

1. **Verify before writing.** Re-read the four target AGENTS.md files. Confirm each candidate rule against current source evidence: Common's actual `#include` surface and project dependency direction, the engine/game symbol contract, the fixed-tick call path through `RunFrameTick` (`Projects/BrokenEngineSandbox/Source/Frame/FrameTick.h`/`.cpp`), the frame CRC and serialization walks described in the two Frame AGENTS.md files, and documented client-only exceptions. Documented behavior must match code before it becomes a tripwire.
2. **`Common/AGENTS.md`, `## Overview` layering sentence.** Rewrite the independence sentence as a dependency tripwire: Common may depend on the standard/third-party headers centralized by its own contract (`ExternalHeaders.h` per `## Headers, Validation, and Platform`) but never on Engine, DataPacker, or Projects/game code. Stop trigger: if a Common change appears to require one of those dependencies, stop before editing and either move the behavior to the higher owning layer or obtain an architectural decision.
3. **`Engine/Source/AGENTS.md`, `## Hub Conventions` bullet "Engine/game contract".** Rewrite the bullet to keep the sanctioned Engine-to-game direction (engine code may consume game types, hooks, globals, and compile-time symbols the game must provide) while making the boundary actionable: an engine-owned shared/public type must not encode a game-only concept unless an existing leaf document records deliberate ownership. Stop trigger: if a new engine type member, enum value, friend, or public signature would name a game-only concept without such a documented exception, stop and surface the ownership decision.
4. **Frame documents — one owner per rule, cross-links elsewhere.** Place each rule at its narrowest owner and cross-link from the other Frame document instead of repeating the full rule:
   - **Fixed-tick input purity** (owner: `Projects/BrokenEngineSandbox/Source/Frame/AGENTS.md`, `## Invariants`): CRC-affecting fixed-tick work receives state through `Frame`, `FrameInput`, `FrameStaticData`, and explicit phase parameters; it must not source decisions from `game::gpGame`, wall-clock time, or client-only render state. Stop trigger: if the required input is unavailable through those contracts, stop and resolve ownership rather than reading a global. This tightens the existing per-tick invariant wording; the sibling snapshot rule ("frame code reads the snapshot, not `gpGame`") in `Projects/BrokenEngineSandbox/Source/AGENTS.md` stays where it is, untouched.
   - **Render-state separation** (owner: `Engine/Source/Frame/AGENTS.md`, first `## Architecture` bullet): PostRender/CRC state must not derive from a client render-interpolated frame or a client-only collection member. The fixed-tick Interpolate state produced inside `RunFrameTick` remains valid simulation input — Interpolate precedes PostRender within the tick per the game Frame invariants — and the tripwire wording must preserve this distinction so it does not forbid existing phase flow.
5. **Contradiction handling.** If source evidence contradicts any candidate rule, do not rewrite the architecture to match the proposal and do not document a false rule. Stop only the affected item, record the exact source/document contradiction as a residual, and require a separate architectural decision plan; independently confirmed items continue.
6. **Size discipline.** Keep every edited document and its effective root-to-leaf chain within the `update-claude-docs` token targets, measured with `pwsh -NoProfile -File .agents/scripts/Measure-Tokens.ps1 -Path <path>` (leaf ≤ 2,000 `bt-token-v1`, hub ≤ 4,000, chain target < 15,000).

## Critical files

- `Common/AGENTS.md` — lower-layer dependency boundary (`## Overview`).
- `Engine/Source/AGENTS.md` — Engine-to-game direction and engine-type ownership boundary (`## Hub Conventions`, "Engine/game contract" bullet).
- `Engine/Source/Frame/AGENTS.md` — render-state separation (first `## Architecture` bullet).
- `Projects/BrokenEngineSandbox/Source/Frame/AGENTS.md` — fixed-tick input purity (`## Invariants`).

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change satisfying the acceptance criteria and add no abstractions, configuration, refactors, or fixes to adjacent prose or code encountered along the way.

**In scope** — only these regions, plus the mechanical necessities (relative cross-links between the two Frame AGENTS.md files) the named edits require:

- `Common/AGENTS.md`: the `## Overview` layering sentence(s) the Design step 2 tripwire replaces.
- `Engine/Source/AGENTS.md`: the **Engine/game contract** bullet under `## Hub Conventions`.
- `Engine/Source/Frame/AGENTS.md`: the first `## Architecture` bullet (Interpolate/PostRender ownership and CRC exclusion) and, if needed, one cross-link line in `## See Also`.
- `Projects/BrokenEngineSandbox/Source/Frame/AGENTS.md`: the `## Invariants` bullets covering `RunFrameTick` sharing and Interpolate/PostRender/CRC ordering and, if needed, one cross-link line in `## See Also`.

Naming a file grants no permission to touch any other section, bullet, table, or link in it.

**Out of scope:**

- Any edit or addition to root `AGENTS.md`.
- Runtime code, include/project dependency changes, API redesign, new architecture, or creation of AGENTS.md files.
- Re-documenting sanctioned leaf exceptions, implementation call chains, or member inventories.
- The `gpGame` snapshot rule in `Projects/BrokenEngineSandbox/Source/AGENTS.md` and every other AGENTS.md not named above.
- Changing determinism, CRC membership, Frame phase behavior, serialization, replay/save compatibility, client/server guards, builds, harness scenarios, or unit tests.

## Risk tier

Tier 1 — mechanical documentation work with no runtime, `kiVersion`/`.pack`, replay/save format, wire protocol, client/server guard, allocation-tracked, shader, build, or live-harness behavior change. Invariant: every documented tripwire must match current code behavior (Design steps 1 and 5); no nondirectional coordination constraint is introduced.

## Acceptance criteria

- Each of the four documents states its confirmed prohibited action and stop trigger at exactly one owning location, with cross-links rather than duplicated full rules, and each tripwire replaces at least as much descriptive prose as it adds.
- A Common dependency scan finds no Engine, DataPacker, or Projects/game dependency contradicting its tripwire.
- The Engine rule still permits documented Engine-to-game symbol use and leaf-owned exceptions while stopping new game concepts in engine-owned shared/public types.
- Frame call-path inspection confirms the fixed-tick versus render-interpolated distinction; a fresh-context dry read of the two Frame documents rejects a CRC-affecting wall-clock/global read and permits PostRender to consume the tick's simulation Interpolate state.
- Any contradiction follows Design step 5 and is reported as a residual instead of being silently normalized.
- `update-claude-docs` sync checks, relative-link checks, `Measure-Tokens.ps1` measurements within the Design step 6 targets, direct coherence review, and `git diff --check` all pass; no root `AGENTS.md`, no other AGENTS.md, and no runtime file changes.

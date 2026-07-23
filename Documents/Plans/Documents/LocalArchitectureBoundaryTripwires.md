<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":["Documents/Plans/Documents/TrimOversizedAgentMemory.md"]} -->
# Local Architecture Boundary Tripwires

## Context

Architecture guidance is strongest when the owning local AGENTS.md states the prohibited action and the exact condition that stops an edit. Current documents describe Common ownership, the sanctioned Engine-to-game dependency direction, deterministic Frame purity, phase separation, and CRC exclusions, but these constraints are distributed among descriptive paragraphs. They should become concise local tripwires without adding more root context or inventing a new architecture.

## Design

1. Re-read the current Common, Engine Source, Engine Frame, and game Frame AGENTS.md files. Verify each candidate boundary against current includes, project dependency direction, fixed-tick call paths, `RunFrameTick`, frame CRC/serialization walks, and documented client-only exceptions. Replace nearby descriptive prose rather than appending duplicate rules.
2. In `Common/AGENTS.md`, state the dependency tripwire: Common may depend on the standard/third-party headers centralized by its own contract but never on Engine, DataPacker, or Projects/game code. If a Common change appears to require one of those dependencies, stop before editing and move the behavior to the higher owning layer or obtain an architectural decision.
3. In `Engine/Source/AGENTS.md`, preserve the sanctioned Engine-to-game direction while making the real boundary actionable: engine code may consume game symbols as already documented, but an engine-owned shared/public type must not encode a game-only concept unless an existing leaf documents deliberate ownership. If a new engine type member, enum value, friend, or public signature would name a game-only concept without such an exception, stop and surface the ownership decision.
4. In the engine and game Frame AGENTS.md files, place each rule at its narrowest owner and cross-link instead of repeating it:
   - CRC-affecting fixed-tick work receives state through `Frame`, `FrameInput`, `FrameStaticData`, and explicit phase parameters; it must not source decisions from `game::gpGame`, wall-clock time, or client-only render state. If the required input is unavailable through those contracts, stop and resolve ownership rather than reading a global.
   - PostRender/CRC state must not derive from a client render-interpolated frame or client-only collection member. The fixed-tick Interpolate state produced inside `RunFrameTick` remains valid simulation input; preserve this distinction so the tripwire does not forbid existing phase flow.
5. If source evidence contradicts any candidate, do not rewrite the architecture to match the proposal and do not document a false rule. Stop the affected item, record the exact source/document contradiction, and require a separate architectural decision plan; independent confirmed items may continue.
6. Keep the effective root-to-leaf documents within the token targets established by `update-claude-docs`. Each tripwire replaces at least as much descriptive prose as it adds.

## Critical files

- `Common/AGENTS.md` — lower-layer dependency boundary.
- `Engine/Source/AGENTS.md` — sanctioned Engine-to-game direction and engine-type ownership boundary.
- `Engine/Source/Frame/AGENTS.md` and `Projects/BrokenEngineSandbox/Source/Frame/AGENTS.md` — fixed-tick input and render-state separation.

## Out of scope

- Any edit or addition to root `AGENTS.md`.
- Runtime code, include/project dependency changes, API redesign, new architecture, or creation of AGENTS.md files.
- Re-documenting sanctioned leaf exceptions, implementation call chains, or member inventories.
- Changing determinism, CRC membership, Frame phase behavior, serialization, replay/save compatibility, client/server guards, builds, harness scenarios, or unit tests.

## Acceptance criteria

- The four local documents state the confirmed prohibited action and stop trigger at one owning location each, with cross-links rather than duplicated full rules.
- Common dependency scans find no Engine, DataPacker, or Projects/game dependency contradicting its tripwire.
- The Engine rule continues to allow documented Engine-to-game symbol use and leaf-owned exceptions while stopping new game concepts in engine-owned shared/public types.
- Frame call-path inspection confirms the fixed-tick and render-interpolated distinction; a fresh-context dry run rejects a CRC-affecting wall-clock/global read and permits PostRender to consume the tick's simulation Interpolate state.
- Any contradiction follows Design step 5 instead of being silently normalized.
- `update-claude-docs` sync checks, relative-link checks, token measurements, direct coherence review, and `git diff --check` pass; no root or runtime file changes.

## Notes

Future implementation is Tier 1 documentation work, but it documents determinism/CRC and layering invariants whose wording must match current code. It has no runtime, `kiVersion`/`.pack`, replay/save format, wire protocol, client/server guard, allocation-tracked, shader, build, or live-harness behavior change. No nondirectional Coordination constraint is introduced.

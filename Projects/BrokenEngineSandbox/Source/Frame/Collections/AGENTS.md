# Collections - Game Entities

## Overview

Game-specific SOA collections built on the engine Collection framework (`../../../../../Engine/Source/Frame/Collections/AGENTS.md`). Children document behavior-specific invariants only.

## Game-Specific Rules

- Transferable entity collections resolve collision/terrain outcomes before marking surviving out-of-bounds rows for Transfer. Passive Targets do not participate in that lifecycle.
- Collision scratch uses heap-suppressed `thread_local` vectors because coord ticks dispatch in parallel and their backing storage must survive through PostCollision; the frame workbuffer cannot own it.
- Collections with client-owned visual/audio objects hydrate them after shared state arrives. Players instead recreate missing owned visuals during Update.
- Players and Spaceships receive a fresh one-second arrival grace timer after transfer. It suppresses targeting and behavior scans, not collision or damage.
- A transfer arrival restores carried state verbatim except arrival grace, which is an intentional fresh one-second arrival default; spawn defaults and fresh random draws otherwise apply only to genuine spawns. Select between them from an explicit transfer marker, not by testing a carried value for zero or non-positive. Two legacy magnitude tests predate the rule and survive as proven non-defects, not counterexamples: Players' `pfFrameChangeTimers` (nothing sets the carried field, so the test always falls through to the fresh draw) and Spaceships' `pfHealths` (the misread it could cause is unreachable — a spaceship at or below zero health explodes before it can be marked for transfer). Do not copy them — and note `SpaceshipsPostRender::SpawnInfo` carries no transfer marker to select on, so adding one is a prerequisite for that collection.
- Shared Update fields are loaded from the previous row and stored to the current row unconditionally. Persistent transition-only fields copied by `AllocateAndCopy` are the documented exception.
- Pack related multi-valued state into the collection `Flags` type where appropriate. Use `/add-collection-member` for every layout change.
- Debug geometry reads positions from the fully interpolated frame; PostRender may supply only flags, metadata, and static world positions.

## Collections

- `Blasters/AGENTS.md`, `Missiles/AGENTS.md`, `Players/AGENTS.md`, `Spaceships/AGENTS.md`, `Targets/AGENTS.md`

## See Also

- `../../../../../Engine/Source/Frame/Collections/AGENTS.md`

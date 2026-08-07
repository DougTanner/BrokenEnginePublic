<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T21:59:18.972Z","dependsOn":[]} -->
# Remove verified-unused includes in game frame collections

## Context

/external-deep-analysis (2026-08-06) architecture Lens A found six unused-include candidates in `Projects/BrokenEngineSandbox/Source/Frame/Collections`; Phase-3 verification confirmed five and refuted one. The refuted one — `Data/Audio.h` in `Blasters/Blasters.cpp` — supports the documented disabled-audio re-enable path (`Blasters/AGENTS.md`) and must stay. The five confirmed removals, each verified against preprocessed production use, direct providers, and the game PCH aggregation policy (`Projects/BrokenEngineSandbox/Source/AGENTS.md`):

1. `Players/Players.h:13` — `Frame/HealthDamage.h`: no declared symbol referenced; consumers include it directly.
2. `Targets/Targets.h:7` — `Frame/GridCoord.h`: no `GridCoord` reference in the header.
3. `Missiles/MissilesUpdate.cpp:9` — `Data/Audio.h`: no `data::` audio constant referenced.
4. `Targets/Targets.cpp:3` — `Frame/FrameStaticData.h`: type never named in the file.
5. `Targets/TargetsUpdate.cpp:3` — `Frame/FrameStaticData.h`: only a reference parameter already covered by the forward declaration in `Targets.h:6`.

## Design

Delete exactly the five `#include` directives above; no other reordering or cleanup.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.h`
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/Targets.h`, `Targets.cpp`, `TargetsUpdate.cpp`
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/MissilesUpdate.cpp`

## In scope

- The five named `#include` lines only.

## Out of scope

- `Blasters/Blasters.cpp` `Data/Audio.h` (deliberately retained); any other include, ordering, or content change.

## Risk tier and invariants

Change Workflow Tier 1 — mechanical, behavior-preserving, no signature or invariant exposure. Invariant: both executables still compile with no new transitive-include reliance.

## Acceptance criteria

- Client and server build clean through `/compile` after removal.

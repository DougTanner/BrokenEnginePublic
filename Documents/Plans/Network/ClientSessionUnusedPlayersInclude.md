<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T12:56:26.016Z","dependsOn":[]} -->
# Remove verified-unused Players.h include from ClientSession.cpp

## Context

/external-deep-analysis (2026-08-07) architecture Lens A flagged, and Phase-3 verification confirmed, that `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp:4` includes `Players.h` while the translation unit references none of its exported symbols (`PlayersInterpolate`, `PlayersPostRender`, `PlayerFlags`/`PlayerFlags_t`, navigation helpers, player tuning constants). The player-related code at `ClientSession.cpp:143-185` uses `ReceivedPlayerEvent`, `engine::global_id_t`, and `Game` methods/state instead. The game PCH already force-includes shared frame layout headers, and consumer TUs must not repeat those includes (`Projects/BrokenEngineSandbox/Source/AGENTS.md`).

The sibling plan `Documents/Plans/Frame/CollectionsUnusedIncludes.md` owns the same class of cleanup for `Frame/Collections` files; this file is outside that plan's explicit `## In scope` boundary, so it is tracked separately.

## Design

Delete exactly the `Players.h` `#include` at `ClientSession.cpp:4`; no other reordering or cleanup.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp`

## In scope

- The single named `#include` line only.

## Out of scope

- Any other include, ordering, or content change in this or any other file.

## Risk tier and invariants

Change Workflow Tier 1 — mechanical, behavior-preserving, no signature or invariant exposure. Invariant: the client executable still compiles with no new transitive-include reliance.

## Acceptance criteria

- Client build clean through `/compile` after removal.

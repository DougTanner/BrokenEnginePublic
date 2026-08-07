# Agent - Game Command Dispatch

## Overview

Game-owned command handlers expose simulation, scene, UI, capture, save/replay, and diagnostic operations through the engine agent transport (`../../../../Engine/Source/Agent/AGENTS.md`). Engine-owned shared commands dispatch first; the game dispatcher then handles game-owned both-endpoint commands before routing remaining commands to its side-specific handlers under `BT_CLIENT` or `BT_SERVER`.

## Contracts

- Treat JSON parameters as hostile external input. Validate required fields, types, ranges, and referenced state; throw to let the engine transport produce a failure response.
- Client handlers own capture, window state, UI/input automation, scene queries, and GPU profile queries. Client `query_profile` remains an empty-object-only command and returns every GPU timer row plus frame-coherent `shadowSample{sequence,currentUs}` telemetry latched from one completed framebuffer; it reads existing main-thread telemetry without forcing query reads.
- Server handlers own simulation controls, status-change injection, frame and CPU profile queries, and save/replay operations. Server `query_profile` may acknowledge one retained raw-profile event by its exact nonzero sequence; `inject_status_changes` may request the matching activation arm only as an exact `{arm:true}` object. The arm is accepted only in a normal unpaused 1/1 server with recording/replay inactive, and validated changes plus the arm commit as one main-thread transaction; an occupied or overrun event rejects the batch before queue mutation. Save/load `file` values are appdata-relative bare filenames: reject empties, embedded NUL, separators, `..`, `:`, and Windows reserved device basenames.
- Replay commands fail when `kbDebugInput` is disabled because the simulation will not consume those requests.
- Replay transfer fixtures are server-only debug controls. Keep their injected state deterministic; a Blaster fixture must materialize a newly live destination's derived terrain grid before selecting a terrain-clear position for both its spawn and first fixed-tick movement, since Blasters destroy themselves on terrain contact.
- Commands that wait for a later frame, for the queue where messages wait for the renderer to pick them up, or for an input script use the engine deferred-response path. Ensure every deferred operation can resolve or fail without blocking the single in-flight channel.
- Scene queries are client-only and read render-visible state. Server frame queries read deterministic simulation state and must preserve collection/type validation.

## See Also

- `../../../../Engine/Source/Agent/AGENTS.md`
- Game Save: `../Save/AGENTS.md`
- `../../../../Tools/AgentHarness/AGENTS.md`

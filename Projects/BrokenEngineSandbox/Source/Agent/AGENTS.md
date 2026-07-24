# Agent - Game Command Dispatch

## Overview

Game-owned command handlers expose simulation, scene, UI, capture, save/replay, and diagnostic operations through the engine [agent transport](../../../../Engine/Source/Agent/AGENTS.md). Engine-owned shared commands dispatch first; the game dispatcher then handles game-owned both-endpoint commands before routing remaining commands to its side-specific handlers under `BT_CLIENT` or `BT_SERVER`.

## Contracts

- Treat JSON parameters as hostile external input. Validate required fields, types, ranges, and referenced state; throw to let the engine transport produce a failure response.
- Client handlers own capture, window state, UI/input automation, scene queries, and GPU profile queries. `query_profile` accepts only an empty object and returns every GPU timer row plus frame-coherent `shadowSample{sequence,currentUs,activePixels}` telemetry latched from one completed framebuffer; it reads existing main-thread telemetry without forcing query reads.
- Server handlers own simulation controls, status-change injection, frame and CPU profile queries, and save/replay operations. Save/load `file` values are appdata-relative bare filenames: reject empties, embedded NUL, separators, `..`, `:`, and Windows reserved device basenames.
- Replay commands fail when `kbDebugInput` is disabled because the simulation will not consume those requests.
- Commands that wait for a later frame, renderer mailbox, or input script use the engine deferred-response path. Ensure every deferred operation can resolve or fail without blocking the single in-flight channel.
- Scene queries are client-only and read render-visible state. Server frame queries read deterministic simulation state and must preserve collection/type validation.

## See Also

- [Engine Agent](../../../../Engine/Source/Agent/AGENTS.md)
- [Game Save](../Save/AGENTS.md)
- [Agent Harness](../../../../Tools/AgentHarness/AGENTS.md)

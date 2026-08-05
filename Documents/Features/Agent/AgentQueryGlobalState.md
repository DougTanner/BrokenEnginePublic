# Agent Query Global State

## Context

Follow-up deferred from the 2026-07-09 AgentHarness3 grill decision. Harness3's `query_*` commands read only the CRC'd Frame SOA collections via `gpGame->CurrentFrame(coord)`. Server-global (non-Frame) state — the things Claude Code will want to inspect when verifying scenarios or diagnosing server behavior — has no query surface. User direction: expose all of it, keeping the `query_*` naming convention so the commands are discoverable alongside the Harness3 set after the AgentHarness6 skill documents the surface.

## Design

New read-only JSON `query_*` arms in `ExecuteAgentCommandServer` (`Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServer.cpp`), running at the drain point (main thread, top of `ServerUpdate`). Candidate surfaces (finalize the exact set at execution):

- `query_fleets` — `ServerFleetManager::mFleets` (`ServerFleetManager.h:90`, GUID-keyed `std::vector<Fleet>` per client): per-fleet members, flagship, wanted coord, nav-controller timers.
- `query_clients` — `ServerClientManager`: connected client slots, GUIDs, owned player ids, subscription coords, `mClientsWaitingForSpawn`.
- `query_session` — tick counter/current time, timescale, pause flag, active coords (`gpGame->mActiveCoords`), replay recording/playback state.

All read-only, server-only, serialized under the AgentHarness1 `Drain()` guard; responses under the 16 MiB cap (offset/limit where a surface can grow with client count). GUID-keyed data serialized with GUIDs as strings so follow-up commands can reference them.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServer.cpp` — new query arms
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.h`, `ServerClientManager.h`, `ServerSession.h` — reference (no edits)

## Out of scope

- Any change to manager state (injection stays Harness3's StatusChange path).
- Client-side global state (graphics/audio/UI — Harness5's `describe_ui` territory).
- Frame-collection queries (landed with Harness3).

## Acceptance criteria

- `query_fleets` on a server with a connected client holding fleets returns the roster with flagship/wanted-coord matching `query_players` per-player fields.
- `query_clients` shows waiting-for-spawn entries while a client connects.
- All commands are read-only: no CRC/replay divergence after issuing every query mid-game.

## Notes

Invariant exposure: none — read-only JSON over server-local state; no wire/CRC/`kiVersion` change. Executes after the AgentHarness6 skill documents the command surface; this plan extends it and updates its command list as a rider.

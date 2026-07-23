<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-09T22:17:50.000Z","dependsOn":[]} -->
# Agent Transport Concurrent Commands

## Context

The agent command transport (engine `AgentCommandServer`, the Harness1 loopback JSON channel) is **single-in-flight**: `listen(..., 1)` backlog (`AgentCommandServer.cpp:34`), and `ServeConnection` reads one request, stores it, then blocks on `mResponseReady` until `Drain()` publishes a response (`AgentCommandServer.cpp:169-182`) — strict request/response lockstep, one request at a time.

When a frame-stepped input script is deferred, `Drain()`'s deferred-poll branch polls the script and **returns early** while it is still pending (`AgentCommandServer.cpp:199-251`, the `if (!result.has_value()) return;` at `:238`) without ever reading `mPendingRequest`. Meanwhile the originating listener thread stays blocked in the `mResponseReady` wait, so it does not even read a second request off the socket. Net effect: **no second command is accepted mid-script.**

This makes two behaviors documented for the AgentHarness5 work **unreachable**:

1. The `busy` error for concurrent input commands — `BeginScriptAndDefer` throws `"busy"` when `AgentInput::BeginScript` returns false (`Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp:334-339`; `AgentInput::BeginScript` at `Engine/Source/Agent/AgentInput.cpp:46`). Since no second command can arrive while a script is deferred, this throw is defensive dead code today.
2. "Non-input commands (`describe_ui` / `screenshot`) remain allowed mid-script" — also unreachable, since the transport accepts nothing until the deferred script response is published.

Verified against current code (`AgentCommandServer::Drain`, `BeginScriptAndDefer`) — premise holds; the transport does **not** accept mid-script commands.

## Design

**Decision plan (present options).** Present the two options and let the user choose:

- **Option A — accept + document (zero code).** The `busy` throw stays as defensive dead code; agents simply wait for script completion before issuing the next command. Update `.agents/skills/agent-harness/SKILL.md`, the durable command-channel owner, to state that the transport is single-in-flight and the two AgentHarness5 concurrency behaviors are aspirational-not-implemented. Cheapest; matches the "rare, lockstep" agent-control usage.
- **Option B — make the documented concurrency real.** Allow non-blocking commands mid-script: accept a new request during deferred polls, execute non-script commands (e.g. `describe_ui` / `screenshot` / `status`) immediately, and reject a second *input* command with the existing `busy` error (making it live). Touches `AgentCommandServer::Drain` accept/response ordering (read `mPendingRequest` even while `mDeferredPoll` is set; interleave a second response) and the listener/response model (currently one `mPendingResponse` slot + lockstep `ServeConnection` wait — would need to correlate responses by id, or pipeline). Larger blast radius in the transport's concurrency model.

## Critical files

- `Engine/Source/Agent/AgentCommandServer.cpp` — `Drain()` deferred-poll branch (`:199-251`), `ServeConnection` request/response lockstep (`:128-189`), `DeferResponse` (`:311`).
- `Engine/Source/Agent/AgentCommandServer.h` — response/request slots, generation bookkeeping.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp` — `BeginScriptAndDefer` (`:334`) and its `busy` throw (`:338`).
- `Engine/Source/Agent/AgentInput.{h,cpp}` — `BeginScript` single-script gate.
- `.agents/skills/agent-harness/SKILL.md` — Option A only: durable single-in-flight command-channel contract; run `/validate-skill` after editing the skill.

## Out of scope

- The async capture defer path itself (`screenshot` / `dump_render_target`) — its single-in-flight defer is by design and stays.
- Multi-connection support / backlog > 1 (the loopback harness is deliberately one connection).
- Any change to the frame-step input-script advancement model.

## Notes

- Client-only harness; no CRC / wire / determinism exposure. The localhost JSON protocol shape may change under Option B (id-correlated / pipelined responses) — note that if B is chosen.
- Residual from the AgentHarness5 work.
- Grill: pick A (accept + doc, recommended for the rare-lockstep usage) vs B (implement live mid-script non-blocking commands).

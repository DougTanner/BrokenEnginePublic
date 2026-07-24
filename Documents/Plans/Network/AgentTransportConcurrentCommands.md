<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-09T22:17:50.000Z","dependsOn":[]} -->
# Agent Transport Concurrent Commands

## Context

The agent command transport (engine `AgentCommandServer`, the harness loopback JSON channel) is **single-in-flight**: `listen(..., 1)` backlog (`Engine/Source/Agent/AgentCommandServer.cpp:64`), and `ServeConnection` reads one request, stores it into `mPendingRequest`, then blocks on the `mResponseReady` condition variable until `Drain()` publishes a response (`AgentCommandServer.cpp:158-219`; the wait at `:199-212`) — strict request/response lockstep, one request at a time.

When a command defers its response (`DeferResponse`, `AgentCommandServer.cpp:360-372` — used by frame-stepped input scripts and async captures), `Drain()`'s deferred-poll branch polls and **returns early** while still pending (`AgentCommandServer.cpp:229-282`; the `return; // still pending — do not accept a new request` at `:269`) without ever reading `mPendingRequest` (consumed only at `:284-293`). Meanwhile the originating listener thread stays blocked in the `mResponseReady` wait, so it never reads a second request off the socket. Net effect: **no second command is accepted mid-script.**

This makes two behaviors documented for the AgentHarness5 work **unreachable**:

1. The `busy` error for concurrent input commands — `BeginScriptAndDefer` throws `"busy"` when `engine::gpAgentInput->BeginScript` returns false (`Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp:1058-1063`, throw at `:1062`; `AgentInput::BeginScript` single-script gate at `Engine/Source/Agent/AgentInput.cpp:46-51`). Since no second command can arrive while a script is deferred, this throw is defensive dead code today.
2. "Non-input commands (`describe_ui` / `screenshot`) remain allowed mid-script" — also unreachable, since the transport accepts nothing until the deferred script response is published.

Verified against current code (`AgentCommandServer::Drain`, `BeginScriptAndDefer`) — premise holds; the transport does **not** accept mid-script commands.

## Design

**Decision plan (present options).** This plan is not decision-complete: before any implementation, present the two options below to the user and implement only the chosen one. Grill: pick A (accept + doc, recommended for the rare-lockstep usage) vs B (implement live mid-script non-blocking commands).

- **Option A — accept + document (zero code).** The `busy` throw stays as defensive dead code; agents simply wait for script completion before issuing the next command. Update `.agents/skills/agent-harness/SKILL.md`, the durable command-channel owner, to state that the transport is single-in-flight and the two AgentHarness5 concurrency behaviors are aspirational-not-implemented. Cheapest; matches the "rare, lockstep" agent-control usage.
- **Option B — make the documented concurrency real.** Allow non-blocking commands mid-script: accept a new request during deferred polls, execute non-script commands (e.g. `describe_ui` / `screenshot` / `status`) immediately, and reject a second *input* command with the existing `busy` error (making it live). Touches `AgentCommandServer::Drain` accept/response ordering (read `mPendingRequest` even while a deferred poll is armed; interleave a second response) and the listener/response model (currently one `mPendingResponse` slot plus the lockstep `ServeConnection` wait — would need to correlate responses by id, or pipeline). Larger blast radius in the transport's concurrency model.

## Scope contract

The listed scope is both target and ceiling: implement the single chosen option as the smallest complete change, and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission beyond the named regions plus the mechanical necessities (includes, declarations) the named change requires.

### In scope — Option A only

- `.agents/skills/agent-harness/SKILL.md` — only the command-channel concurrency contract text: state single-in-flight behavior and mark the two AgentHarness5 concurrency behaviors aspirational-not-implemented. No other skill content changes. Run `/validate-skill` after editing.
- No C++ changes.

### In scope — Option B only

- `Engine/Source/Agent/AgentCommandServer.cpp` — `Drain()` (deferred-poll branch and `mPendingRequest` consumption ordering), `ServeConnection` (request/response lockstep wait), `DeferResponse`, and `PublishResponse` as the response-correlation change requires.
- `Engine/Source/Agent/AgentCommandServer.h` — the request/response slot members (`mPendingRequest`, `mPendingResponse`), deferred-state members (`mDeferredPoll`, `mDeferredId`, `mbResponseDeferred`, `muiDeferredGeneration`, `miDeferredDrainCount`), generation bookkeeping (`muiConnectionGeneration`), and the class contract comments they carry.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp` — `BeginScriptAndDefer` only (`:1058-1063`); its existing `busy` throw becomes live. No other command handler changes.
- `Engine/Source/Agent/AgentInput.{h,cpp}` — `AgentInput::BeginScript` single-script gate only, and only if the accept path requires an adjustment; the gate semantics (one script at a time) are unchanged.
- `.agents/skills/agent-harness/SKILL.md` — only if the observable channel contract changes (id-correlated / pipelined responses); run `/validate-skill` after editing.

### Out of scope (both options)

- The async capture defer path itself (`screenshot` / `dump_render_target`) — its single-in-flight defer is by design and stays.
- Multi-connection support / backlog > 1 (the loopback harness is deliberately one connection).
- Any change to the frame-step input-script advancement model (`AgentInput` script phases).
- Any other command handler, transport framing, or socket lifecycle code not named above.

## Risk tier

- **Option A: Tier 1** — documentation/skill-only, no code.
- **Option B: Tier 3** — changes the transport's cross-thread request/response model (listener thread vs main-thread `Drain()`), a threading surface. Invariants to preserve: no game mutations or Vulkan/UI work on the socket thread; responses never land in a later connection's stream (connection-generation discard); the deferred-poll liveness timeout still resolves a lost async result; steady-state main-loop code stays allocation-tracked (`ScopedSuppressAllocationTracking` covers JSON work). Client-only harness; no CRC / wire(game protocol) / determinism exposure — the localhost JSON protocol shape may change (id-correlated / pipelined responses); document that shape in the harness skill if B is chosen.

## Acceptance criteria

- **Option A:** `.agents/skills/agent-harness/SKILL.md` states the single-in-flight contract and marks both concurrency behaviors as not implemented; `/validate-skill` passes. No code diff.
- **Option B (observable, via `/agent-harness`):** while a frame-stepped input script is deferred, (1) a `describe_ui` (or `screenshot` / `status`) request issued mid-script returns a normal success envelope before the script's response; (2) a second input command issued mid-script returns the `busy` failure envelope; (3) the deferred script still completes and publishes its own response under its original id; (4) client build compiles.

## Notes

- Residual from the AgentHarness5 work.

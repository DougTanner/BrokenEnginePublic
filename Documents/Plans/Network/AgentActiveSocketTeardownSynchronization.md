<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-28T20:14:57.659Z","dependsOn":[]} -->
# Synchronize Agent Active-Socket Teardown

## Context

`AgentCommandServer::~AgentCommandServer` closes `mActiveSocket` under `mMutex` (`Engine/Source/Agent/AgentCommandServer.cpp:105-108`), while `ListenerLoop` calls `ServeConnection` after releasing that mutex (`:164-174`). `ServeConnection` performs blocking `recv` through `ReadExact` (`:193-202`) and later sends the response on the same local socket. The destructor and listener can therefore operate on the same active socket concurrently.

The accepted external-claim review refuted `closesocket` concurrent with a blocking Winsock call as supported teardown synchronization. The current close-to-unblock idiom is consequently an unsupported synchronization boundary, not a proven hang. Reachability is **low/unknown**: no teardown hang has been observed, but current source permits the unsupported interleaving.

This is a proven out-of-scope residual from the Tier-3 listener-descriptor fix. `Documents/Plans/Network/AgentListenSocketRecycleWindow.md` explicitly excludes `mActiveSocket` and the `ServeConnection` receive path, and the user approved no active-socket behavior expansion. It is a separate threading fix, not a change to the loopback JSON protocol.

## Design

Replace only the active-connection teardown synchronization so shutdown never relies on `closesocket` racing a listener-thread blocking receive or send.

- Establish one active-socket lifetime owner at each phase. The destructor may request shutdown and signal the selected cancellation path, but it must not close an active descriptor while `ServeConnection` can still use its local socket value. The listener performs the final close and invalidation after connection I/O has exited, under the existing transport lock.
- Select a bounded or explicitly cancellable receive/send strategy whose relevant Winsock behavior is verified from an official source before implementation. Do not infer that `closesocket`, `shutdown`, polling, or nonblocking mode supplies the required cross-thread guarantee from memory. If the verified behavior changes the current blocking-mode restoration, update its exact transport documentation.
- Preserve the listener-side invariant: nonblocking listener `accept`, transport-lock serialization of listener invalidation and publication, stop-before-close/wake ordering, and no mutex held across connection I/O or game-state dispatch.
- Keep the existing single-in-flight framing, response generation, connection-generation cleanup, loopback binding, and public interface unchanged unless the verified cancellation mechanism mechanically requires a private declaration.

## Critical files

- `Engine/Source/Agent/AgentCommandServer.cpp` — `~AgentCommandServer`, `ListenerLoop`, `ServeConnection`, and `ReadExact`/`SendExact`: active-socket shutdown, I/O exit, and final close ownership only.
- `Engine/Source/Agent/AgentCommandServer.h` — only private state or helper declarations mechanically required by the verified cancellation mechanism; preserve `mListenerThread` member order.
- `Engine/Source/Agent/AGENTS.md` — only the active-connection teardown wording if the selected mechanism changes its durable transport invariant.

## Out of scope

- Listener-socket accept/recycle behavior, including the nonblocking accept cadence and its teardown serialization, which belongs to `AgentListenSocketRecycleWindow.md`.
- JSON framing, command dispatch, deferred-response semantics, connection-generation policy, multi-connection support, listen backlog, or loopback/bind policy.
- Deterministic simulation, CRC, replay, save/serialization, `.pack`/`kiVersion`, wire protocol, client/server affinity, and metrics tooling.
- Adding a generic socket abstraction, retries, configuration, or a synthetic repro beyond the acceptance scenarios.

## Risk tier and invariants

**Tier 3 — threading.** This changes cross-thread ownership and cancellation of a descriptor shared between the destructor and listener `jthread`.

- No thread may call `closesocket` on the active connection while another can execute a blocking receive or send on that connection.
- Shutdown must remain bounded: the listener exits connection I/O, finalizes the active descriptor exactly once, and the `jthread` join completes without holding `mMutex` across blocking I/O.
- A stop observed before active-socket publication still closes the local accepted socket without publication or `ServeConnection`.
- No determinism/CRC, serialization, replay, `.pack`/`kiVersion`, or wire-protocol exposure; this remains agent-port-gated loopback transport.

## Acceptance criteria

- An external-claim packet verifies the selected Winsock cancellation behavior for the supported Windows configuration before implementation. Together with structural ownership/path inspection of every active-socket receive, send, stop, and final-close path, this is the decisive evidence for the unsupported concurrent-close defect.
- Structural inspection proves one final-close owner after connection I/O exits, no destructor-side concurrent close of `mActiveSocket`, and no lock held across `ServeConnection` or other blocking connection I/O.
- Debug x64 client and server builds compile the changed transport source.
- Via `/agent-harness`, ordinary client and server launch/ping/quit is regression smoke only: it must preserve clean exit and expected logs, but it makes no claim to hold an idle connection in a next-frame receive or to cover a pending response.
- Existing ping/request-response framing, deferred-response behavior, and a normal quit remain unchanged; no protocol, CRC, replay, or serialization artifact changes are introduced.

## Notes

- The external review establishes only that concurrent `closesocket` is not a supported synchronization primitive. It does not pre-approve a replacement primitive; verify the chosen mechanism during implementation.
- No observed hang is claimed by this Plan. Structural and controlled-shutdown evidence are the required signals for a low/unknown-reachability teardown defect.

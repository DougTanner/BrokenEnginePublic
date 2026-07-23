<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Agent Listener accept() Socket-Descriptor Recycle Window

## Context

**Pre-existing latent defect. Not introduced by any recent change** — the identical snapshot-then-`accept` sequence is present at baseline commit `41699997` (`git show 41699997:Engine/Source/Agent/AgentCommandServer.cpp`, same code at its `ListenerLoop`) and is unchanged in the current tree. Found by an adversarial review while falsifying an unrelated hypothesis about a compile-firewall refactor of this class; that refactor was subsequently rejected and reverted, so it never affected this code path.

`AgentCommandServer::ListenerLoop` (`Engine/Source/Agent/AgentCommandServer.cpp:86-96`) snapshots the listen socket under `mMutex`, **releases the lock**, and only then calls `accept()` on the snapshot:

```
SOCKET listenSocket = INVALID_SOCKET;
{
    std::unique_lock lock(mMutex);
    listenSocket = mListenSocket;
}
if (listenSocket == INVALID_SOCKET) { break; }

SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
```

`~AgentCommandServer` (`AgentCommandServer.cpp:50-70`) closes `mListenSocket` under the same mutex and stores `INVALID_SOCKET`, relying on the close to unblock a listener already parked in `accept()`. If the destructor closes the handle in the window **between the unlock and the `accept()` call**, the listener enters `accept()` on a stale descriptor value. Windows recycles socket descriptor values, so if the process opens a new socket in that window the stale value can name an unrelated live socket — `accept()` would then target it and could block indefinitely, hanging the `std::jthread` join. `mListenerThread` is deliberately the last member of `AgentCommandServer` (`AgentCommandServer.h:82`), so it is destroyed first and joins only after the destructor body has closed the sockets.

**Reachability assessment: LOW.** This is correctness hardening, not a live bug report — no hang has been observed in practice. The window is a handful of instructions wide, and no socket creation is sequenced during agent-server teardown, so nothing is positioned to recycle the descriptor value there. A future reader should score and prioritize it on that basis and not assume a reproducible hang exists.

## Design

Close the gap between the snapshot and the `accept()` so a closed descriptor cannot be handed to `accept()` after the destructor has released it. The listener must still be unblockable from the destructor without holding `mMutex` across a blocking `accept()` (which would deadlock the destructor's own close).

Pre-stage the mechanism rather than assuming one; the destructor's unblock contract is the binding constraint:

- **A — keep the descriptor alive past the close.** Defer the actual `closesocket` of the listen socket until after the listener thread joins, using `shutdown()` or a stop-signalled non-blocking/poll path to break `accept()`. Removes the recycle hazard outright; changes teardown ordering, so the destructor's documented close-before-join contract (`AgentCommandServer.cpp:52-54`) must stay correct.
- **B — make the wait interruptible without a raw blocking `accept`.** Park on `select`/`WSAPoll` with a short timeout plus a `stop_token` re-check, re-reading `mListenSocket` under the lock each iteration so a closed handle is observed before any blocking call on it.

Both remove the unlocked stale-handle use; pick the one that keeps the destructor's existing close-to-unblock contract intact with the least new state.

## Critical files

- `Engine/Source/Agent/AgentCommandServer.cpp` — `ListenerLoop` snapshot and `accept()` (`:84-96`); `~AgentCommandServer` socket close under `mMutex` (`:50-70`); the end-of-connection close (`:111-120`).
- `Engine/Source/Agent/AgentCommandServer.h` — `mListenSocket` (`:66`), `mActiveSocket` (`:67`), `mMutex` (`:69`), and `mListenerThread` (`:82`, deliberately the last member so it joins last).
- `Engine/Source/Agent/AGENTS.md` — the documented "background `jthread` owns socket I/O" transport contract; update if teardown ordering changes.

## Out of scope

- `mActiveSocket` and the `recv` path in `ServeConnection` — the same close-to-unblock idiom applies there, but this plan is scoped to the listen socket unless the chosen mechanism makes the connection socket a mechanical follow-on.
- The single-in-flight request/response model and the deferred-poll branch — owned by `Documents/Plans/Network/AgentTransportConcurrentCommands.md`, a different root cause (`Drain` lockstep, not descriptor lifetime).
- Multi-connection support, `listen()` backlog changes, and any protocol change.

## Acceptance criteria

1. No code path passes a `SOCKET` value to a blocking call after the mutex protecting its lifetime has been released, without re-validating it under that lock.
2. Client and server both compile (`/compile`); the agent server is `--agent-port`-gated engine code present in both builds.
3. **Runtime teardown check via `/agent-harness`**: launch client and server with an agent port, issue `ping`, then `quit`; both processes exit cleanly with no join hang and no listener error logged. Repeat with a shutdown issued while a connection is live (`quit` immediately after a command) to exercise the destructor-during-`accept` ordering. Every harness session already exercises this teardown, so a regression here surfaces immediately and loudly.

The defect itself is not reproducible on demand (LOW reachability, no sequenced descriptor recycle at teardown), so criterion 1 is a structural check, not an observed-failure check. Do not manufacture a repro to claim otherwise.

## Notes

- **No determinism/CRC, `.pack`/`kiVersion`, replay, or wire-protocol exposure.** Loopback JSON control channel only; agent-mode gated, never on a routable interface (`AgentCommandServer.cpp:20-23`, `INADDR_LOOPBACK`).
- **Exposed invariant is threading/teardown ordering**: the destructor must remain able to unblock the listener without holding `mMutex` across a blocking call, and `mListenerThread` must stay the last-declared member so its join runs after the destructor body closes the sockets (`AgentCommandServer.h:82`).
- Trust boundary unchanged: the socket is external input and its parse path already throws on invalid frames; this plan touches lifetime only.
- Do not add an `ASSERT` on the descriptor value — a stale-but-valid recycled handle is exactly the case an assert cannot distinguish.

<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Agent Listener accept() Socket-Descriptor Recycle Window

## Context

**Pre-existing latent defect. Not introduced by any recent change** — the identical snapshot-then-`accept` sequence is present at baseline commit `41699997` (`git show 41699997:Engine/Source/Agent/AgentCommandServer.cpp`, same code at its `ListenerLoop`) and is unchanged in the current tree. Found by an adversarial review while falsifying an unrelated hypothesis about a compile-firewall refactor of this class; that refactor was subsequently rejected and reverted, so it never affected this code path.

`AgentCommandServer::ListenerLoop` (`Engine/Source/Agent/AgentCommandServer.cpp:112-126`) snapshots the listen socket under `mMutex`, **releases the lock**, and only then calls `accept()` on the snapshot:

```
SOCKET listenSocket = INVALID_SOCKET;
{
    std::unique_lock lock(mMutex);
    listenSocket = mListenSocket;
}
if (listenSocket == INVALID_SOCKET)
{
    break;
}

SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
```

`~AgentCommandServer` (`AgentCommandServer.cpp:80-100`) closes `mListenSocket` under the same mutex and stores `INVALID_SOCKET`, relying on the close to unblock a listener already parked in `accept()`. If the destructor closes the handle in the window **between the unlock and the `accept()` call**, the listener enters `accept()` on a stale descriptor value. Windows recycles socket descriptor values, so if the process opens a new socket in that window the stale value can name an unrelated live socket — `accept()` would then target it and could block indefinitely, hanging the `std::jthread` join. `mListenerThread` is deliberately the last member of `AgentCommandServer` (`AgentCommandServer.h:87`), so it is destroyed first and joins only after the destructor body has closed the sockets.

**Reachability assessment: LOW.** This is correctness hardening, not a live bug report — no hang has been observed in practice. The window is a handful of instructions wide, and no socket creation is sequenced during agent-server teardown, so nothing is positioned to recycle the descriptor value there. A future reader should score and prioritize it on that basis and not assume a reproducible hang exists.

## Design

Close the gap between the snapshot and the `accept()` so a closed descriptor cannot be handed to `accept()` after the destructor has released it. The listener must still be unblockable from the destructor without holding `mMutex` across a blocking `accept()` (which would deadlock the destructor's own close).

Pre-stage the mechanism rather than assuming one; the destructor's unblock contract is the binding constraint:

- **A — keep the descriptor alive past the close.** Defer the actual `closesocket` of the listen socket until after the listener thread joins, using `shutdown()` or a stop-signalled non-blocking/poll path to break `accept()`. Removes the recycle hazard outright; changes teardown ordering, so the destructor's documented close-before-join contract (the `~AgentCommandServer` comment, `AgentCommandServer.cpp:82-84`) must stay correct.
- **B — make the wait interruptible without a raw blocking `accept`.** Park on `select`/`WSAPoll` with a short timeout plus a `stop_token` re-check, re-reading `mListenSocket` under the lock each iteration so a closed handle is observed before any blocking call on it.

Both remove the unlocked stale-handle use; pick the one that keeps the destructor's existing close-to-unblock contract intact with the least new state.

Whichever mechanism is chosen, the change is the smallest one that removes the unlocked stale-handle window; it introduces no new configuration, no multi-connection capability, and no change to the request/response protocol.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change that satisfies the acceptance criteria and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the regions named below plus the mechanical necessities (includes, declarations, comment updates on the exact regions changed) the named change requires.

**In scope:**

- `Engine/Source/Agent/AgentCommandServer.cpp`
  - `AgentCommandServer::ListenerLoop` — only the pre-connection region: the `mListenSocket` snapshot, the `INVALID_SOCKET` break, and the `accept()` call (`:112-132`). The post-`accept` body (`mActiveSocket` publish, `ServeConnection` call, end-of-connection teardown block `:141-154`) changes only if the chosen mechanism mechanically requires it for the listen socket.
  - `~AgentCommandServer` (`:80-100`) — only the listen-socket close/unblock sequencing and its contract comment; the `mActiveSocket` close and `mResponseReady.notify_all()` stay as-is.
  - `AgentCommandServer::AgentCommandServer` — only if the chosen mechanism needs listener-thread or listen-socket setup changes (e.g., non-blocking mode); the bind/retry/listen logic (`:10-72`) is untouched.
- `Engine/Source/Agent/AgentCommandServer.h`
  - Declarations only as the mechanism requires: `mListenSocket` (`:71`), any new private member or method for the unblock mechanism, and the `mListenerThread` last-member comment (`:87`) if teardown ordering changes. No other member, constant, or public-API change.
- `Engine/Source/Agent/AGENTS.md` — the "background `jthread` owns socket I/O" / teardown wording in Architecture, only if teardown ordering actually changes.

**Out of scope:**

- `mActiveSocket` and the `recv` path in `ServeConnection` — the same close-to-unblock idiom applies there, but this plan is scoped to the listen socket unless the chosen mechanism makes the connection socket a mechanical follow-on.
- The single-in-flight request/response model, `Drain()`, `DeferResponse()`, and the deferred-poll branch — a different root cause (`Drain` lockstep, not descriptor lifetime).
- The constructor's `SO_REUSEADDR` bind-retry policy and its comments.
- Multi-connection support, `listen()` backlog changes, and any protocol change.

## Critical files

- `Engine/Source/Agent/AgentCommandServer.cpp` — `ListenerLoop` snapshot and `accept()` (`:112-132`); `~AgentCommandServer` socket close under `mMutex` (`:80-100`); the end-of-connection close (`:141-154`).
- `Engine/Source/Agent/AgentCommandServer.h` — `mListenSocket` (`:71`), `mActiveSocket` (`:72`), `mMutex` (`:74`), and `mListenerThread` (`:87`, deliberately the last member so it joins last).
- `Engine/Source/Agent/AGENTS.md` — the documented "background `jthread` owns socket I/O" transport contract; update if teardown ordering changes.

## Risk tier

**Tier 3 — threading.** The change alters cross-thread teardown ordering between the destructor (main/startup thread) and the listener `jthread`; threading is excluded from Tier 2. Invariants to preserve:

- The destructor must remain able to unblock the listener without holding `mMutex` across a blocking call.
- `mListenerThread` must stay the last-declared member so its join runs after the destructor body has done its unblock work (`AgentCommandServer.h:87`).
- No determinism/CRC, `.pack`/`kiVersion`, replay, or wire-protocol exposure — loopback JSON control channel only.

## Acceptance criteria

1. No code path passes a `SOCKET` value to a blocking call after the mutex protecting its lifetime has been released, without re-validating it under that lock.
2. Client and server both compile (`/compile`); the agent server is `--agent-port`-gated engine code present in both builds.
3. **Runtime teardown check via `/agent-harness`**: launch client and server with an agent port, issue `ping`, then `quit`; both processes exit cleanly with no join hang and no listener error logged. Repeat with a shutdown issued while a connection is live (`quit` immediately after a command) to exercise the destructor-during-`accept` ordering. Every harness session already exercises this teardown, so a regression here surfaces immediately and loudly.

The defect itself is not reproducible on demand (LOW reachability, no sequenced descriptor recycle at teardown), so criterion 1 is a structural check, not an observed-failure check. Do not manufacture a repro to claim otherwise.

## Notes

- **No determinism/CRC, `.pack`/`kiVersion`, replay, or wire-protocol exposure.** Loopback JSON control channel only; agent-mode gated, never on a routable interface (`AgentCommandServer.cpp:14-18`, `INADDR_LOOPBACK`).
- Trust boundary unchanged: the socket is external input and its parse path already throws on invalid frames; this plan touches lifetime only.
- Do not add an `ASSERT` on the descriptor value — a stale-but-valid recycled handle is exactly the case an assert cannot distinguish.

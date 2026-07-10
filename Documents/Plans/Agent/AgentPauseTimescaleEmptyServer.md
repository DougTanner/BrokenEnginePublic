# Agent pause / timescale Silently Revert on a Zero-Client Server

## Summary
**What this plan does:** Edge-gates the empty-client state revert in `ServerClientManager::Disconnects()` (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.cpp:265-272`). Today that block clears `kPaused` and resets `mTimeStep` to 1/1 every tick while `GetClients().empty()`; the fix makes it fire only when the drain loop actually processed a disconnect and the client set is now empty (the last client just left). Doc updates follow in `Network/Server/CLAUDE.md` and the agent-harness SKILL caveat.

**Why it's good for the codebase:** On a zero-client server, agent `pause` and `timescale` commands report success but are silently reverted one tick later, and `inject_status_changes` can never report `deferred:true` — a real gap for headless agent-harness scenarios (verified by a live harness validation run). The fix preserves the documented "resume at 1× when the last client disconnects" intent exactly while letting agent-commanded state hold on an empty server.

## Context
- Source: Documents/Plans/Agent/AgentPauseTimescaleEmptyServer.md (claimed; removed with its Order.md row after execution completes)
- Order.md row: Tier Small / Effort 2 / Impact 2 / Risks 1 / Score 1 (marked `[CLAIMED]` in Step 2)
- Notes: **Decision plan (present options).** Harness validation run: agent `pause`/`timescale` on a zero-client server report success but revert next tick — `ServerClientManager::Disconnects()` clears kPaused / resets 1× every tick while `GetClients().empty()`, not on a disconnect edge; `deferred:true` injection unreachable without a client. Edge-gate the revert vs. accept + keep the SKILL.md caveat. Adjacent to `Network/ServerPauseAndResetSemantics.md`, scopes disjoint
- Relevance: Fully — every citation verified line-exact against current source (`Disconnects()` `:242-273`, revert block `:265-272`; `CommandPause` `AgentCommandsServer.cpp:66-75`, `CommandTimescale` `:77-87`, `deferred` gate `:308`; SKILL.md caveat lines 87/184)
- Dependency resolution: none — no `## Dependencies` bullet names this plan; the `ServerPauseAndResetSemantics` adjacency is a coordination note with explicitly disjoint scopes
- Changes since the plan was written: `CommandTimescale` now routes through `ServerSession::StepTimescale(bFaster)` (steps + broadcasts to clients) instead of raw `mTimeStep` steps — consistent with the plan; no approach change

## Mechanism (verified)

`ServerClientManager::Disconnects()` (`ServerClientManager.cpp:242-273`) runs every tick and, after draining pending disconnects (`:247-262`), unconditionally executes (`:265-272`):

```cpp
if (engine::gpServer->GetClients().empty())
{
	gpGame->mGameFlags.Clear(engine::GameFlags::kPaused);
	if (gpGame->mTimeStep.miTimeMultiply != 1 || gpGame->mTimeStep.miTimeDivide != 1)
	{
		gpGame->mTimeStep.SetTimeScale(1, 1);
	}
}
```

The intent (Server/CLAUDE.md: "Disconnect clears `kPaused` and restores `mTimeStep` to 1/1 once no clients remain") is "resume ticking at 1× for the next connection." But the guard is a level condition evaluated every tick, not a disconnect edge — with zero clients it fires continuously and reverts agent-commanded state one tick after the command applied it. Verified repro: 0 clients → `pause {paused:true}` returns `{"paused":true}` but `status` shows `paused:false`; `timescale faster` reports 2/1, then an immediate `timescale slower` steps from the reverted 1/1 to 1/2. Downstream: `inject_status_changes` can never report `deferred:true` (`AgentCommandsServer.cpp:308`, gated on `kPaused`) without a client.

## Design — decision (RESOLVED at grill: Option A)

**Grill decisions (2026-07-09):** Option A (edge-gate). Connect-while-paused: an agent-held pause deliberately persists when a client connects (attach-while-paused harness workflow); the client can unpause via its own pause request. `kPaused` has no wire broadcast (timescale does, via the ClientHello catch-up send `ServerSession.cpp:476`) — a client connecting into an agent-held pause sees a frozen world with no signal; accepted, document in the SKILL caveat update.

### Option A (chosen) — revert only on a real disconnect edge

Track whether the drain loop (`:247-262`) processed any disconnect this call; gate the `:265-272` block on `(bProcessedDisconnect && engine::gpServer->GetClients().empty())`.

- Pros: preserves the documented "resume for the next connection" intent exactly (fires the moment the last client disconnects), while a server that starts empty — or that an agent paused with no clients — keeps agent-commanded state. Smallest change, no new flag on `Game`.
- Cons: a never-connected empty server keeps whatever pause/timescale the agent set — the desired behavior here, but a behavior change worth noting.

### Option B — agent-pause exemption flag

Add a flag (e.g. `GameFlags::kAgentHeldPause`) that agent `pause`/`timescale` set, exempting the revert while held.

- Cons: new persistent flag + an unclear release path; more machinery than the edge check. Not recommended.

### Option C — document as a hard constraint, no code change

Keep the SKILL caveat and reject the fix.

- Cons: leaves `deferred:true` and agent pause/timescale unreachable on an empty server — a real gap for headless harness scenarios.

**Relation to `Network/ServerPauseAndResetSemantics.md`** (adjacent, unlanded): that plan preserves request queues drained while paused (`BuildFrameInputs`/`PreTickNetwork` gating) and explicitly does not touch `Disconnects()` or the empty-client revert. This plan owns only the `Disconnects()` revert edge. No shared edit sites; do not duplicate its scope.

## Execution steps

1. `Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.cpp` — `Disconnects()` (`:242-273`): track whether the `DrainPendingDisconnects()` loop (`:247-262`) processed ≥1 disconnect; gate the unpause/timescale-reset block (`:265-272`) on that edge AND `GetClients().empty()`. (Assumes Option A at grill.)
2. `Projects/BrokenEngineSandbox/Source/Network/Server/CLAUDE.md` — update the Notes line "Disconnect clears `kPaused` and restores `mTimeStep` to 1/1 once no clients remain" to the edge wording ("when the last client disconnects").
3. `.claude/skills/agent-harness/SKILL.md` — update the two caveat sites (line 87: "Both require ≥1 connected client to stick…", line 184: "Pause/timescale need a connected client…") to reflect that pause/timescale now hold on an empty server and `deferred:true` is reachable headless; note that an agent-held pause persists when a client connects (no pause wire catch-up — the client sees a frozen world until it unpauses).

## Additional candidate locations

- `Projects/BrokenEngineSandbox/Source/Frame/FrameTick.cpp:33,:45,:57` — per-coord lazy build of derived data gated on `container.empty()` as a proxy for a "just created/reloaded" edge. **Oversight**, **Related**, high confidence, **not folded**: same level-as-edge-proxy shape but a different concern (rebuilds derived data, doesn't clobber commanded state; benign today) and already owned by the live plan `Frame/FrameTickMinorHardening.md` (its "NavData empty-vertices rebuild sentinel → explicit built-flag" item). No action here.
- No other candidates: the sweep traced every per-tick flag/timescale/mode reset across server managers, engine tick loops, `FrameTick.cpp`, `Game.cpp`, client session, and agent command files — all other mutation sites are genuine edges (keypress, packet arrival, session reset). No superseding umbrella plan exists.

## Out of scope

- Request-queue preservation while paused — owned by `Network/ServerPauseAndResetSemantics.md`.
- The wire timescale broadcast (`BroadcastTimespeedIfChanged`) and the ClientHello catch-up send — unchanged.
- Any change to how `CommandPause`/`CommandTimescale`/`inject_status_changes` apply state — they are correct; the bug is the revert, not the apply.
- Client-side pause/timescale behavior.

## Acceptance criteria

- With zero clients: `pause {paused:true}` holds (`status` shows `paused:true`, tick not advancing) until an explicit `pause {paused:false}`; `timescale faster` then `timescale slower` steps 1/1 → 2/1 → 1/1 (from the actually-applied state, not a reverted 1/1).
- A server that had clients and loses the last one still resumes at unpaused / 1× (documented intent preserved).
- `inject_status_changes` on a paused zero-client server reports `deferred:true`.

## Notes

- **Invariant exposure.** Server tick-orchestration / client-manager path. No wire/CRC/`kiVersion`/determinism-math change; gameplay-visible (pause/tick-rate) → needs playtest or harness verification.
- **Decisions resolved at grill (2026-07-09).** Option A (edge-gate) chosen over B (exemption flag) and C (document only). Connect-while-paused: keep — agent-held pause persists across a client connecting; documented, not cleared and no pause catch-up send added.

## Verification (agent-harness)

1. Launch server headless with `--agent-port 27100`, zero clients.
2. `pause {"paused":true}` → `status` shows `paused:true` across ≥3 polls; tick counter frozen.
3. `timescale {"faster":true}` → 2/1; `status` confirms; `timescale {"faster":false}` → back to 1/1.
4. `pause {"paused":true}` then `inject_status_changes` → response has `deferred:true`.
5. Connect a client, disconnect it → `status` shows `paused:false`, timescale 1/1 (edge revert preserved).

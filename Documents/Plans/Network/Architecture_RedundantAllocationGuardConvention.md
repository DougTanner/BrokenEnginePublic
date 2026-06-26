# Redundant Inner Allocation-Suppression Guards Under Orchestrator Blanket Guards (Server Managers)

## Context

`common::ScopedSuppressAllocationTracking` is a **thread-local counter**, not a boolean. While the counter is positive the allocation tracker is silenced; a nested inner guard under an outer guard increments the counter and suppresses *nothing extra* (its ctor and dtor cancel out within the outer scope). The complementary `ScopedResumeAllocationTracking` decrements the counter to re-arm the tracker for a callee that was intentionally migrated off the heap (`ServerSession::BroadcastTick` already does this around `BroadcastStatusChanges`).

Three orchestrator methods hold blanket guards covering whole server-manager sub-pipelines:

- `game::ServerSession::PreTickNetwork` (`ServerSession.cpp`, guard ~`:263`) — drains the per-manager request queues.
- `game::ServerSession::BroadcastTick` (`ServerSession.cpp`, guard ~`:75`) — post-tick broadcast + death detection.
- `game::ServerBroadcaster::BuildFrameInputs` (`ServerBroadcaster.cpp`, guard ~`:20`) — runs inside `PrepareTick`/`PrepareActiveSet` every `ServerUpdate`.

Two more orchestrator-altitude guards cover their own callees: `ServerSession::PrepareTick` (~`:55`) and `ServerSession::ComputeActiveSet` (~`:342`, itself called under guarded callers).

Because of these blanket guards, **most public manager entry points re-declare their own inner `ScopedSuppressAllocationTracking` that is redundant** — it protects a real allocator with an accurate `// Heap:` comment, but the outer guard already silences the tracker for the whole call. A minimal cleanup just landed in `ServerFleetManager.cpp` that (a) fixed three stale `// Heap:` comments and (b) removed **one** truly-redundant guard (`ProcessDeleteFleetRequests`, which protected no real allocator). That cleanup deliberately left the broader convention question for this follow-up: now that one inner guard is gone, the "every entry point self-guards" convention is half-applied across the managers.

`Projects/BrokenEngineSandbox/Source/Network/Server/CLAUDE.md` documents the inner guards under its **"Allocation suppression"** invariant as *intentional defense-in-depth* ("public manager entry points that grow SOA buffers or member containers wrap their body in `ScopedSuppressAllocationTracking`"). Whichever option is chosen, that invariant is the doc to update or clarify.

This is **server-only** (`BT_SERVER`). It touches **only** allocation-tracking comments and guard scoping — **no** CRC / determinism / wire-format / `game::Frame::kiVersion` / `.pack` layout / replay exposure. The tracker is a debug-build diagnostic; removing a guard never changes shipped behavior, only whether a stray main-loop heap allocation trips `DEBUG_BREAK()` in tracked builds.

## Design

**Decision plan — present options.** The core decision: should server-manager entry-point inner guards that are redundant under an orchestrator blanket guard be kept as documented defense-in-depth, or removed wholesale for consistency?

### Option 1 — KEEP as documented belt-and-suspenders (recommended)

Leave the redundant inner guards in place. Optionally add a one-line clarifying note to the `Server/CLAUDE.md` "Allocation suppression" invariant making explicit that these inner guards are *defensive redundancy* under the orchestrator blanket guards (the counter semantics mean they cost nothing at runtime), so a future caller that invokes a manager method *outside* an orchestrator guard stays protected.

Pros:
- Zero risk. Each guard protects a **real** allocator with an **accurate** comment; nothing breaks.
- Future-proofs against new call paths that don't sit under a blanket guard (e.g. a new direct caller of a fleet/client manager method), where the inner guard becomes load-bearing again.
- The cost of a misjudged *removal* is a spurious `DEBUG_BREAK()` that may only surface on a rarely-hit branch — asymmetric downside.

Cons:
- The convention stays "redundant by design," which can read as noise to a newcomer.
- The just-landed removal of the `ProcessDeleteFleetRequests` guard now makes the convention inconsistent (one entry point has no inner guard).

### Option 2 — REMOVE the redundant inner guards wholesale

Delete the inner `ScopedSuppressAllocationTracking` (and its now-orphaned `// Heap:` comment) at every entry point whose **every** call path is already covered by an orchestrator blanket guard, relying solely on the orchestrator guards. Update the `Server/CLAUDE.md` invariant to state the new rule: *suppression is owned at the orchestrator altitude; manager entry points do not self-guard.*

Pros:
- Single, consistent rule; less to read; matches the direction the fleet-sync cleanup started.
- Removes ~13 lines of guards + comments.

Cons:
- Each removal requires re-confirming that **every** caller of that method is outer-guarded — a future or overlooked unguarded call path turns into a `DEBUG_BREAK()`.
- The orchestrator guards then carry the protection for code that is physically distant from them; the locality cue ("this method allocates") is lost.
- Pure consistency/cosmetic win; no runtime or correctness benefit.

### Recommendation

**Lean Option 1 (KEEP).** These guards protect real allocators, the comments are accurate, and the runtime cost is zero (counter semantics). The blast radius of a misjudged removal (a spurious `DEBUG_BREAK` on a cold branch) outweighs the cosmetic consistency gain. Pick Option 2 only if the user explicitly wants the orchestrator-owns-suppression simplification. If Option 1, the minimal deliverable is the one-line `Server/CLAUDE.md` clarification (or no change at all).

### Redundant inner-guard sites (verified redundant under an outer blanket guard; all protect REAL allocators with ACCURATE comments — NOT stale)

Cited by **symbol** (line numbers drift — the just-landed cleanup already shifted them; re-confirm at execution):

- `ServerFleetManager.cpp`:
  - `DetectDisconnectedPlayerDeaths` — outer `BroadcastTick`.
  - `ReadFleetData` — outer = all four `game::GameSaveLoad::ReadGrid` callers (`Save/GameSaveLoad.cpp`).
  - `ProcessSpawnIntoFleetRequests` — outer `PreTickNetwork`.
  - `ProcessRespawnInFleetRequests` — outer `PreTickNetwork`.
  - `TickFleetTimers` — outer `BuildFrameInputs`.
  - `ProcessFlagshipUpdates` — outer `BuildFrameInputs`.
- `ServerClientManager.cpp`:
  - `ProcessSpawnRequests`, `NewClients`, `Disconnects` — outer `PreTickNetwork`.
  - `FinalizeNewClients`, `DetectPlayerDeaths` (block-scope guard) — outer `BroadcastTick`.
- `ServerBroadcaster.cpp`:
  - `ProcessUpdatePlayerRequests` — outer `BuildFrameInputs`.
- `ServerSession.cpp`:
  - `ComputeActiveSet` — callers all guarded (`PrepareTick` + `GameSaveLoad` paths).
  - `HandleResyncRequests` — outer `BroadcastTick`.

> NOTE: `ServerFleetManager.cpp` has additional self-guarded entry points not in this list (e.g. fleet create / `TryRelinkNewClient` / `TryRelinkClientForLoad` / `OnResetForLoad`). If Option 2 is chosen, the execution audit must enumerate **all** inner guards in the four files and classify each call path — this list is the verified-redundant subset, not an exhaustive census.

### LOAD-BEARING — exclude / do NOT remove

- `ServerTransferManager.cpp` `HarvestTransfers` (guard ~`:212`, comment ~`:211`): its caller `game::Game::HarvestTransfers` (`Game.cpp` ~`:379`, invoked from `engine::GameBase::ServerUpdate`'s transfer-harvest step) is **UNGUARDED** — the inner guard is the only protection. Keep.
- The three orchestrator guards themselves (`PreTickNetwork`, `BroadcastTick`, `BuildFrameInputs`) plus `PrepareTick`/`ComputeActiveSet` — these *are* the blanket coverage. Keep.

### Secondary item (separate, smaller — stale `// Heap:` send-comment verification)

Two low-confidence "possibly stale" caller-side comments to investigate. **Preliminary inspection suggests both are ACCURATE, not stale** — record/confirm, do not reflexively "fix":

- `game::ServerSession::HandleResyncRequests` (`ServerSession.cpp` ~`:451`) — comment "per-resync per-slot SendCoordFullState allocates serialization buffers." The prompt's premise (engine impl "now workbuffer-based at `ServerSend.cpp:125`") is **imprecise**: `ServerSend.cpp:125` is a *different* function (the `WriteBufferedFramePacket`/coord-update path). The actual `engine::Server::SendCoordFullState` (`ServerSend.cpp:13`) **still** serializes via `std::ostringstream frameStream` + `std::string frameData` (`:26-28`) before LZ4 — only the final packet assembly moved to the workbuffer. So the comment is **accurate**. Likely outcome: no change (or a precision tweak). Re-verify the function body at execution.
- `game::ServerSession::BroadcastTick` (`ServerSession.cpp` ~`:74`) — comment "SendFullState, SendAssignPlayer, and BroadcastUpdate allocate for serialization and compression." Given `SendCoordFullState` still uses `ostringstream` and the `CompressToBuffer`/`mCompressionBuffer` path can grow, this comment is also **likely accurate**. Verify carefully before any change.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp` — six redundant-guard sites (above).
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.cpp` — five redundant-guard sites.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerBroadcaster.cpp` — `ProcessUpdatePlayerRequests` (redundant) + the `BuildFrameInputs` orchestrator guard (keep).
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — `ComputeActiveSet`/`HandleResyncRequests` (redundant); `PreTickNetwork`/`BroadcastTick`/`PrepareTick` orchestrator guards + the `:85` `ScopedResumeAllocationTracking` re-arm (keep); secondary comment sites `~:74`/`~:451`.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerTransferManager.cpp` — `HarvestTransfers` (LOAD-BEARING, exclude).
- `Projects/BrokenEngineSandbox/Source/Game.cpp` — `Game::HarvestTransfers` (~`:379`, the unguarded caller proving `HarvestTransfers` is load-bearing; read-only reference).
- `Engine/Source/Network/Server/ServerSend.cpp` — `Server::SendCoordFullState` (`:13`) for the secondary-comment verification (read-only reference; engine-side, do not edit guard scope here).
- `Projects/BrokenEngineSandbox/Source/Network/Server/CLAUDE.md` — "Allocation suppression" invariant: update (Option 2) or lightly clarify (Option 1).

## Out of scope

- The three orchestrator blanket guards and the `PrepareTick`/`ComputeActiveSet` guards — not removed under either option.
- `ServerTransferManager::HarvestTransfers` inner guard — explicitly excluded (load-bearing; caller unguarded).
- Engine-side guards in `Engine/Source/Network/Server/ServerSend.cpp` / `ServerReceive.cpp` / `Server.cpp` — this plan is the **game-layer** server managers only; the engine send path's own guards (e.g. `SendCoordFullState`'s `:24` guard, `SendPacket`'s self-suppression) stay as-is.
- Any change to allocation-tracker mechanics, `ScopedResumeAllocationTracking` placement, or the `BroadcastStatusChanges` re-arm (that landed via a prior plan).
- The fleet-sync send no-guard rationale already documented in the "Allocation suppression" invariant (`SendFleetSyncToClient` etc.) — unchanged.
- CRC / determinism / wire format / `kiVersion` / `.pack` layout / replay — none touched.
- Reducing or splitting `ServerFleetManager.cpp` (808 lines, a separate `/reduce-file` candidate) — not this plan.

## Acceptance criteria

- A decision is recorded (Option 1 KEEP vs Option 2 REMOVE) with the user's confirmation.
- If Option 1: the `Server/CLAUDE.md` "Allocation suppression" invariant is unchanged or gains a one-line note that the inner manager-entry guards are defensive redundancy under the orchestrator blanket guards (zero-cost counter semantics). No source change.
- If Option 2: every removed inner guard's call paths were re-audited and confirmed outer-guarded at execution time (an explicit per-site call-path list); `HarvestTransfers` and the orchestrator guards remain; the orphaned `// Heap:` comments are removed with their guards; the `Server/CLAUDE.md` invariant is rewritten to state suppression is owned at the orchestrator altitude. Server build compiles clean and a server tick with at least one connected client runs without a tracker `DEBUG_BREAK()`.
- The two secondary `// Heap:` send-comments are verified against current source and either confirmed accurate (no change) or corrected — not blindly edited.

## Notes

- **Tag:** Decision plan (present options).
- **Diagnosis Discipline (mandatory if Option 2):** line numbers in this plan are pre-cleanup snapshots and the just-landed fleet-sync edit already shifted `ServerFleetManager.cpp`. Before removing any guard, re-derive each method's callers from current source and prove every path sits under an orchestrator guard — do not trust the line citations. A missed unguarded path = spurious `DEBUG_BREAK()` on a cold branch.
- **Single open decision for `/external-grill-plan`:** Option 1 (KEEP, recommended) vs Option 2 (REMOVE wholesale).
- Sibling/context: the `BroadcastTick` `ScopedResumeAllocationTracking` re-arm around `BroadcastStatusChanges` (the "armed-by-design callee under a blanket guard" pattern) already landed; this plan is the complementary concern (redundant *inner* guards), not that re-arm.
- Server-only (`BT_SERVER`); debug-tracking diagnostic only — no shipped-behavior, CRC, determinism, wire, or version exposure.

# Fleet-Sync Sends Are Never Armed — Outer Blanket Guards Dominate the Inner FleetManager Guards

## Context

Spun out of `Network/BroadcastTickAllocationGuardScope.md` (landed). That plan armed `BroadcastStatusChanges`
inside `ServerSession::BroadcastTick` via a `ScopedResumeAllocationTracking` scope, because `BroadcastTick` is a
top-level `ServerUpdate` step that enters with the tracker armed (`Main.cpp` calls `ServerUpdate` outside any
suppress scope), so `BroadcastTick`'s own function-scope guard was the *sole* suppressor.

A codebase sweep for the same pattern flagged five "sibling" sites that wrap the workbuffer-based, armed-by-design
`game::SendFleetSync` (`ServerFleetManagerUtils.cpp`, reached via `ServerFleetManager::SendFleetSyncToClient`) in a
function-scope `ScopedSuppressAllocationTracking`:

- `ServerFleetManager::ProcessCreateFleetRequests`
- `ServerFleetManager::ProcessDeleteFleetRequests`
- `ServerFleetManager::OnPlayerSpawned`
- `ServerFleetManager::OnClientConnected`
- `ServerFleetManager::OnResetForLoad`

**Verification during the BroadcastTick grill proved the sweep's framing wrong.** Suppression is a thread-local
*counter* (`Common/AllocationTracking.h`), and every path to `SendFleetSync` runs inside an **outer blanket
guard**, not just the inner FleetManager guard:

- `ServerSession::PreTickNetwork` opens a function-scope `ScopedSuppressAllocationTracking` covering its whole
  request-drain — which calls `ProcessCreateFleetRequests` / `ProcessDeleteFleetRequests` directly, plus
  `mpClientManager->NewClients()` (→ `OnClientConnected`) and `mpClientManager->ProcessSpawnRequests()`
  (→ `OnPlayerSpawned`). So all four of those sends run with the counter already positive from `PreTickNetwork`.
- `ServerSession::ResetClientsForLoad` opens a function-scope guard covering its per-client loop, which calls
  `OnResetForLoad` (→ the fifth send).
- The sweep's claimed "correct armed reference," `ServerFleetManager::UpdateFleetNavigationDelay` (no inner
  guard), is itself called from `ParseReceivedGamePackets`, which runs under `PreTickNetwork`'s guard — so it is
  **also** suppressed. `SendFleetSync` is in fact *never* armed on any path.

Consequences:

1. Narrowing (or removing) the inner FleetManager guards does **nothing** — the outer `PreTickNetwork` /
   `ResetClientsForLoad` guards keep the counter positive across the send. The originally-proposed "narrow the
   caller's guard" fix would be a no-op at these sites.
2. The five FleetManager guards are therefore **redundant nested guards** under the outer guards.
3. The `// Heap: … SendFleetSyncToClient allocates packet` comments on several of those guards are **stale** — the
   send was migrated to the workbuffer and no longer heap-allocates; the genuine allocators in those functions are
   the `mFleets` / `mGuidToClientId` / `mPlayerToGuid` map growth and member-vector growth, not the send.
4. Unlike `BroadcastStatusChanges`, `SendFleetSync` is **not documented anywhere as an armed-by-design callee** —
   its armed intent is only implicit (workbuffer + no inner guard). So there is no written invariant currently
   being violated; the question is whether to *extend* the armed-tracker discipline to it.

## Design — Decision plan (present options)

Grill must pick the disposition; verify the cited call chains before editing (Diagnosis Discipline — they drift
under concurrent edits, re-confirm line numbers at execution).

- **Option A — accept + document (recommended).** `SendFleetSync` is cheap and runs in the request-drain where
  lots of genuine allocation already happens under the blanket guard; arming it in isolation has low value.
  Keep the outer guards. Do the *cleanup* only: delete the redundant inner FleetManager guards at the five sites
  (they suppress nothing the outer guards don't already) and fix the stale `// Heap: … allocates packet` comments
  to name the real allocators. Record in `Server/CLAUDE.md` that fleet-sync sends run under the request-drain /
  load-path blanket guards and are intentionally *not* independently armed.
- **Option B — arm the sends.** Narrow the outer `PreTickNetwork` and `ResetClientsForLoad` guards so each
  `SendFleetSync` runs with the counter at zero. This is a larger restructuring of the per-tick request-drain and
  the load path: the drain allocates heavily (ENet polling, packet parsing, spawn/new-client processing), so the
  outer guard cannot simply be removed — it must be split around each armed send (likely via per-allocator tight
  guards or `ScopedResumeAllocationTracking` scopes threaded down to each `SendFleetSyncToClient` call). Higher
  effort and higher chance of a spurious `DEBUG_BREAK` if a real allocator is left unguarded. Only worth it if the
  armed-tracker guarantee on fleet-sync is judged valuable.

Recommendation: **A** — the inner guards are provably redundant and the stale comments are actively misleading, so
the cleanup is pure upside; full arming (B) is a disproportionate change for a send that no invariant requires to
be armed.

## Out of scope

- **The `BroadcastTick` → `BroadcastStatusChanges` fix** — already landed (`ScopedResumeAllocationTracking` scope
  in `ServerSession::BroadcastTick`); this plan does not revisit it.
- **The `ScopedResumeAllocationTracking` primitive itself** (`Common/AllocationTracking.h`) — unchanged; Option B
  would *use* it, not modify it.
- **Adding or removing real allocations** in the fleet path — the goal is guard scoping / comment accuracy, not
  changing what allocates.
- **Determinism / wire format** — none touched; the `SendFleetSync` wire payload and ordering are unchanged.
  (`OnPlayerSpawned` serializes `iFlagshipIndex` in the send, so under Option B the send must not be reordered
  relative to the flagship-index update.)

## Acceptance criteria

- Option A: the five FleetManager sites no longer carry a redundant `ScopedSuppressAllocationTracking`; their
  comments name the real allocators (map / member-vector growth), not the send; `Server/CLAUDE.md` states the
  fleet-sync sends are not independently armed and why; server builds and runs with no spurious allocation break.
- Option B: each `SendFleetSync` executes with the suppression counter at zero (tracker armed); every genuine
  allocator in `PreTickNetwork` / the fleet methods / `ResetClientsForLoad` remains covered by a narrowed guard;
  server builds and runs with no spurious allocation break.
- No CRC / replay / wire-format change either way.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp` — `ProcessCreateFleetRequests`,
  `ProcessDeleteFleetRequests`, `OnPlayerSpawned`, `OnClientConnected`, `OnResetForLoad`, `SendFleetSyncToClient`,
  and the `UpdateFleetNavigationDelay` reference path.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — `PreTickNetwork` (the request-drain
  blanket guard) and `ResetClientsForLoad` (the load-path blanket guard); also `ParseReceivedGamePackets`
  (the `UpdateFleetNavigationDelay` caller).
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.cpp` — `NewClients`
  (→ `OnClientConnected`) and `ProcessSpawnRequests` (→ `OnPlayerSpawned`).
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManagerUtils.cpp` — `game::SendFleetSync`
  (the workbuffer-based, armed-by-design leaf).
- `Projects/BrokenEngineSandbox/Source/Network/Server/CLAUDE.md` — the "Allocation suppression" invariant (doc
  target).
- Read-only reference: `Common/AllocationTracking.h` (the thread-local counter + the two RAII guards),
  `Engine/Source/Memory/GlobalAllocator.cpp` (the `> 0` check the counter feeds).

## Notes

- **Server-only build** (`BT_SERVER`); no client / CRC / determinism / wire-format / `kiVersion` exposure. Risk is
  low for Option A (redundant-guard removal + comment fix), moderate for Option B (restructuring a hot,
  heavily-allocating request-drain risks a spurious `DEBUG_BREAK` if a real allocator is left unguarded).
- One grill decision staged: accept + cleanup (A, recommended) vs arm the sends (B).
- Tagged "Decision plan (present options)".

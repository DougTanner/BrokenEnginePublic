# TryRelinkClientForLoad Lacks the Global-ID Sort the Reconnect Path Has

## Context

The server has two client-relink paths that match persisted/live players back to a (re)connecting client by
`ClientGuid` and re-issue assign/spawn packets. They are **inconsistent** in ordering:

- **Reconnect path** — `ServerClientManager::TryRelinkNewClient`
  (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.cpp:122-164`) collects matches into a
  `relinkEntries` vector, then **sorts by global ID** before assigning:
  ```cpp
  // Re-link with existing players by matching ClientGuid (sorted by global ID to preserve creation order)  (:124)
  std::ranges::sort(relinkEntries, [](const RelinkEntry& l, const RelinkEntry& r)
      { return l.globalId.iValue < r.globalId.iValue; });                                                    (:149-151)
  for (const auto& rEntry : relinkEntries) { rNewClientOwnedIds.push_back(rEntry.globalId); SendAssignPlayer(...); ... }
  ```
  The comment is explicit: the sort exists **to preserve creation order**.

- **Load path** — `ServerSession::TryRelinkClientForLoad`
  (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp:524-551`) iterates
  `gpGame->mCoordFrames` (a map) and, within each coord, the `PlayersPostRender` array in **index order**,
  pushing each match straight into `rLoadOwnedIds` and sending assign/spawn **with no sort**:
  ```cpp
  for (const auto& [rCoord, rFrames] : gpGame->mCoordFrames) {
      for (int64_t i = 0; i < rPlayers.iCount; ++i) {
          if (rPlayers.pClientGuids[i] == rClient.clientGuid) {
              rLoadOwnedIds.push_back(rPlayers.pGlobalPlayerIds[i]);   // encounter order, not global-ID order
              SendAssignPlayer(...); SendPlayerState(..., kSpawned, ...); ...
          }
      }
  }
  ```

So after a **load**, a client's owned-player order in `rLoadOwnedIds` (and the assign/spawn send order) is the
`mCoordFrames` map iteration order × per-frame array index order — **not** global-ID (creation) order. After a
**reconnect**, it *is* global-ID order. This is the asymmetry the `Network/Server/CLAUDE.md` hub already notes
("relink sort asymmetry (reconnect sorts by global ID, load does not)").

The question is whether this is **intentional** (load order doesn't need to match creation order) or a
**missing sort** (load should preserve creation order just like reconnect, e.g. so the client's local
flagship/owned-player ordering is stable across save→load). `rLoadOwnedIds` becomes the client's
`mClientOwnedPlayerIds` entry, which downstream death-detection (`DetectPlayerDeaths`) and fleet ownership
consult — order-sensitivity there is the thing to verify.

## Design

This is a **behavior/intent decision** — resolve in the grill, then either add the sort or document the
difference.

- **Option A — add the sort (make load match reconnect).** Collect the load matches into a local vector of
  `{globalId, coord}` entries (mirroring `TryRelinkNewClient`'s `RelinkEntry`), `std::ranges::sort` by
  `globalId.iValue`, then push to `rLoadOwnedIds` and send assign/spawn in sorted order. This makes the two
  relink paths consistent and gives save→load the same creation-order guarantee reconnect has. Recommended if
  any consumer of `mClientOwnedPlayerIds` order is order-sensitive (verify `DetectPlayerDeaths` /
  fleet-ownership / flagship selection during the grill). Determinism-adjacent: the assign/spawn **send order**
  is client-visible; making it deterministic and matching the reconnect path is the safer posture.
- **Option B — document the intentional difference.** If the user confirms load order is irrelevant (the client
  re-derives ordering, nothing keys on `mClientOwnedPlayerIds` position), leave the code and add a one-line
  comment at `TryRelinkClientForLoad` stating that load relink does **not** sort (unlike reconnect) and why,
  so the asymmetry stops reading as an oversight.

Recommendation: lean **A** unless the grill establishes order genuinely doesn't matter — the reconnect path's
explicit "preserve creation order" comment is evidence that *something* cares about order, and load should not
silently diverge from it. The fix is small (a local entries vector + one `std::ranges::sort`, copied from the
sibling).

## Out of scope

- `TryRelinkNewClient` (the reconnect path) — already sorted; this plan only addresses the load path's
  inconsistency, not the reconnect path.
- The `ClientGuid`-matching logic, the `authorizedCoords` push, and the assign/spawn packet contents — only
  the **ordering** of the matched set changes (Option A) or is documented (Option B).
- The broader `ResetClientsForLoad` flow (`ServerSession.cpp:~490-522`) and the fleet-manager
  `OnResetForLoad` restoration — unchanged.
- The unrelated stray-backslash typo on the comment at `ServerSession.cpp:509` (`\ Fleet manager: ...`) — note
  only; not part of this finding (surface separately if a sweep touches the file).
- Save/replay format or `kiVersion` — load relink does not touch serialized layout.

## Acceptance criteria

- After a save→load, a relinked client's owned-player ids (and the assign/spawn send order) are in the same
  order convention as after a reconnect — global-ID/creation order (Option A) — OR the intentional difference
  is documented in-code (Option B), per the grill decision.
- A normal save→load still relinks every owned player exactly once (no dropped/duplicated assigns).
- Server builds clean.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — `TryRelinkClientForLoad`
  (`:524-551`); the unsorted match loop at `:532-549` is the change/decision site.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.cpp` — `TryRelinkNewClient`
  (`:122-164`); the sorted reference implementation (`RelinkEntry` + `std::ranges::sort` at `:149-151`) to
  mirror under Option A.
- `Projects/BrokenEngineSandbox/Source/Network/Server/CLAUDE.md` — already documents the "relink sort
  asymmetry"; update if the code is changed (standard doc step).

## Notes

- Server-only. The assign/spawn **send order** is client-visible, so this is determinism-adjacent (replicated
  ordering), though not part of the shared-CRC sim path; treat with playtest care if Option A lands.
- One grill decision: add the sort (A) vs document the difference (B). Resolve by checking whether any consumer
  of `mClientOwnedPlayerIds` ordering (death detection, fleet ownership, flagship choice) is order-sensitive.

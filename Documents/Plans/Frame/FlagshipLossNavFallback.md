# Flagship-Loss Navigation Fallback

## Context

Confirmed by the 2026-07-03 Frame review sweep: a wingman in flagship-follow navigation (mode 5) has **no exit path** when the flagship disappears. The flagship-follow block (`PlayersNavigation.cpp:209-241`) only enters mode 5 or updates its destination when a `kIsFlagship` player exists in the scan; the mode-5 branch (`PlayersNavigation.cpp:261-297`) has no arrival check (that is mode-4-only — noted at the `:357` comment). When the flagship is destroyed and removed, a follower keeps pathfinding to the stale `rVecIslandDestination` (last flagship position; W stays 1 so mode 4's entry check never fires — and no flip occurs anyway), hovering there indefinitely (still drawing its 3 randoms/tick, so RNG parity is unaffected) until a `kUpdateFleet`/fleet-override or respawn StatusChange arrives. Deterministic on both sides — a stuck-AI bug, not a desync.

Whether Game-level respawn orchestration always rescues promptly is **unverified** — implementation must check that first (see Design 1).

## Design

1. **Verify the rescue window.** Trace the server-side flagship-death → respawn/fleet-update flow (`FleetNavigationController`, Game respawn orchestration) and measure the realistic gap between flagship removal and the next `kUpdateFleet` reaching followers. If the gap is provably one tick, downgrade this plan to a comment at the mode-5 branch and close.
2. **Add the in-frame fallback** (assuming a real gap): in the flagship-follow scan, when no `kIsFlagship` player is found and the entity is in mode 5, exit to the island-seek mode (mode 4) via the existing mode-transition path so the normal arrival/re-target machinery takes over. Keep the transition free of new RNG draws or draw-count changes on any path (both sides compute identically, but the per-mode draw counts are part of the deterministic stream — preserve the 3-draws/tick shape or change it identically in all branches).
3. Prefer expressing the fallback with the existing nav-mode `Flags` helpers; no new SOA state.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp` — flagship-follow block, mode-5 branch, mode-transition helpers
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.h` — nav-mode flag helpers (read; touch only if a helper is missing)

## Out of scope

- Game-level respawn orchestration changes — the Frame purity constraint keeps flagship-tracking responsibilities at Game level; this plan only adds the frame-local fallback for the gap.
- Fleet reassignment policy (which player becomes the new flagship) — Game/fleet-manager territory.
- Mode 4/5 steering behavior itself (TerrainUtils contour-following) — unchanged.

## Acceptance criteria

- With the flagship removed and no fleet StatusChange arriving, a mode-5 follower transitions to island-seek within one tick of the scan finding no flagship, on both client and server, with identical RNG draw counts on both.

## Notes

- **Invariant exposure.** Inside the `/fp:strict` CRC'd tick; changes CRC'd nav behavior (mode transitions) — replay-visible, no layout/wire/`kiVersion` change required. RNG draw-count parity is the main hazard: any new/removed draw must occur identically on both sides (it will — the scan conditions on shared state only).
- **Single open decision for `/external-grill-plan`:** fallback target — flip to mode 4 island-seek (recommended; reuses arrival machinery) vs hold position but mark for re-tasking vs adopt the nearest surviving fleet member as an interim leader. Resolve after Design 1's rescue-window verification.
- Shares `PlayersNavigation.cpp` with `Frame/TransferSentinelConflation.md` (transfer build region) — co-schedule or refresh citations.

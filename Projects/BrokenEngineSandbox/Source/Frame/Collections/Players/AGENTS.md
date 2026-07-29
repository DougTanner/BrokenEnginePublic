# Players - Player Fleet Ships

Players represent a flagship and AI-driven wingmen through one collection. Stable global IDs provide lookup and transfer identity; flagship status is a flag, never a privileged SOA index.

## Navigation Invariants

- Navigation covers roaming, cross-frame movement, island destinations, and flagship following. Mode transitions depend only on shared state.
- Flagship following has no frame-local exit when the flagship row is gone; recovery comes from the server's flagship reassignment landing in the next tick's Update phase, not from navigation. On D+2, the promoted flagship's reassigned flag, carried mode 5, and same-cell wanted coord recover it to mode 4 and clear the stale destination; this preserves mode 4's three-draw schedule and leaves the fleet's 60-second rally timer unchanged.
- Path queries run on a deterministic cadence staggered by tick and stable global ID, with immediate recomputation for mode, destination, or direction changes.
- The cadence also breaks off-schedule when a navigation polygon contains either the lookahead point along the carried steering bearing or the current position. Both tests are needed: containment ahead does not imply containment at the position, and a ship already driven inside sits there as its steady state. Key this on nav-polygon containment, not a terrain-elevation probe — the polygon is that elevation contour inflated outward, so a ship stranded in the band between them measures clear terrain and never re-paths.
- Random draws, arrival checks, and mode transitions remain outside the pathfinding throttle. Modes 4 and 5 do not have an identical per-tick draw schedule; parity comes from both builds evaluating the same shared-state conditions.
- Spawning consumes its frame-change random draw even when transfer data already supplies the resulting timer.
- Preserve cached steering and client debug waypoints across ticks where pathfinding is skipped.

## State Boundaries

- Game identity uses stable global IDs rather than local collection indices.
- Shared CRC excludes client animation time, local pusher bookkeeping, and server-minted global identity. Some excluded state still participates in save or transfer.

# Players - Player Fleet Ships

Players represent a flagship and AI-driven wingmen through one collection. Stable global IDs provide lookup and transfer identity; flagship status is a flag, never a privileged SOA index.

## Navigation Invariants

- Navigation covers roaming, cross-frame movement, island destinations, and flagship following. Mode transitions depend only on shared state.
- Flagship following has no frame-local exit when the flagship row is gone; recovery comes from the server's flagship reassignment landing in the next tick's Spawn phase, not from navigation. The promoted ship is the exception — it satisfies neither the follow scan nor the fleet override, so it holds the mode until the fleet navigation timer fires.
- Path queries run on a deterministic cadence staggered by tick and stable global ID, with immediate recomputation for mode, destination, direction, or terrain-avoidance changes.
- Random draws, arrival checks, and mode transitions remain outside the pathfinding throttle. Modes 4 and 5 do not have an identical per-tick draw schedule; parity comes from both builds evaluating the same shared-state conditions.
- Spawning consumes its frame-change random draw even when transfer data already supplies the resulting timer.
- Preserve cached steering and client debug waypoints across ticks where pathfinding is skipped.

## State Boundaries

- Game identity uses stable global IDs rather than local collection indices.
- Shared CRC excludes client animation time, local pusher bookkeeping, and server-minted global identity. Some excluded state still participates in save or transfer.

## See Also

- [../AGENTS.md](../AGENTS.md) - Game collection transfer, CRC, and collision rules

# SmokeTrails - Owner-Driven Smoke Ribbons

Client-only smoke trails are owner-synced base-plane ribbons deposited into the screen-space smoke simulation. Explosions and missiles own their types and lifetimes.

## Invariants

- Smoothed tail positions persist across frame copies. Full-state reception patches them from the previous ring frame so reconciliation does not visibly reset trails.
- Rendering divides deposit quantity by the current segment length. Do not strengthen this implementation fact into a general frame-rate or speed-independence guarantee.
- New trails briefly suppress length until movement establishes a segment. Cross-cell missile transfer reuses the trail identity and bypasses that spawn delay so the rebound trail continues immediately.
- Both ribbon endpoints project to base height; width remains perpendicular to the segment on the base plane.

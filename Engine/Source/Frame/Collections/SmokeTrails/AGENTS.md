# SmokeTrails - Owner-Driven Smoke Ribbons

Client-only smoke trails are owner-driven base-plane ribbons deposited into the screen-space smoke simulation. Explosions and missiles own their types and lifetimes.

## Invariants

- Smoothed tail positions persist across frame copies. Full-state reception patches them from the previous ring frame so reconciliation does not visibly reset trails.
- Rendering divides deposit quantity by the current segment length. Do not strengthen this implementation fact into a general frame-rate or speed-independence guarantee.
- New trails briefly suppress length until movement establishes a segment. Cross-cell missile transfer reuses the trail identity and bypasses that spawn delay so the rebound trail continues immediately.
- Both ribbon endpoints project to base height; width remains perpendicular to the segment on the base plane.
- The smoothed tail position column overloads its W component: W == 0 marks a slot with no smoothed history yet, so that update copies the owner's position instead of easing toward it and W becomes 1 with it. This is a deliberate exception to the repository rule that positions carry W = 1.0. Do not read W as a real coordinate and do not clear it on a live trail "for safety" — that trail's smoothed position then snaps and visibly pops. The smoothing rate is a fixed constant in `SmokeTrailsUpdate.cpp`, not a `gSmokeTrails*` tweak value.
- Rendering draws per-quad jitter from an unseeded file-scope random engine. That is render-only client state: it never reaches the simulation or the CRC, so it is not a determinism bug and must not be made deterministic.

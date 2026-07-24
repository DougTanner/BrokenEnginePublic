# Puffs - Controlled Smoke Sprites

Client-only stationary smoke puffs are fire-and-forget controlled quads. Consumers register puff types and controllers; this collection has no owner-driven sync path.

## Invariants

- Puff lifetime and animation live in Interpolate; the paired PostRender collection only maintains phase compatibility and expires controlled rows.
- Area and intensity wrappers are read during every controller update, so wrapper changes affect existing puffs. Rotation is not wrapper-scaled.
- `PersistentMembers()` copies type/controller/start-time metadata between frames, while animated values are derived again and position is carried forward unchanged.
- A future start time holds the puff at its first keyframe, supporting delayed secondary effects.
- Rendered puffs are culled and projected to base height before depositing smoke.

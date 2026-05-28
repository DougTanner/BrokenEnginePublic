# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/

Trackable world positions for missile guidance, referenced by stable ID.

## Game-Specific Behavior

- **Passive collection**: `Register()`, render, and every phase method are intentional no-ops. State mutates only through an explicit Add/Remove/AddSubscriber/Sync API invoked by owning Spaceships and homing Missiles — no visual representation, no independent phase participation.
- **Split storage**: Interpolate is the `kIdToIndex` side and holds the position + type index that owners drive each tick via `IdToIndex`; PostRender (paired, non-indexable) holds the liveness state — id, flags, subscriber count, alignment. Position lives on Interpolate because owners write it during Sync (W forced to 1.0).
- **Dual-ownership liveness**: An entry is freed only when its owner has released its `kDestination` reference AND subscriber count is zero. `Remove` has two modes keyed by the passed flags — `kDestination` (owner death) tears down immediately regardless of subscribers; an empty-flags call is subscriber release, which decrements the count and frees only if the owner already cleared `kDestination`.
- **Owner-set destination flag**: The owning Spaceship sets the entry's `kDestination` flag (deferred until its arrival grace period expires, so missiles don't home mid-arrival). Missiles validate a tracked target each tick by confirming its row still exists and still carries `kDestination`, releasing otherwise.
- **Alignment filtering**: Missile target selection rejects entries whose alignment can't collide with the seeker's (same-team rejection).
- **Custom type registration**: Target types push directly into the Interpolate collection's `TypeRegistry`, bypassing the standard `Register()` pathway.

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)

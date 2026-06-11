# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/

Trackable world positions for missile guidance, referenced by stable ID.

## Game-Specific Behavior

- **Passive collection**: `Register()`, render, and every phase method are intentional no-ops — the empty bodies exist only to satisfy the engine `ForEach*` dispatch interface, so don't remove them. State mutates only through an explicit Add/Remove/AddSubscriber/Sync API invoked by owning Spaceships and homing Missiles — no visual representation, no independent phase participation. Unlike its siblings, Targets has no client-only fields and no `BT_CLIENT` code at all.
- **Split storage**: Interpolate is the `kIdToIndex` side and holds the position + type index that owners drive each tick via `IdToIndex`; PostRender (paired, non-indexable) holds the liveness state — id, flags, subscriber count, alignment. Position lives on Interpolate because owners write it during Sync (W forced to 1.0).
- **Dual-ownership liveness**: An entry is freed only when its owner has released its `kDestination` reference and subscriber count is zero. `Remove` has two modes keyed by the passed flags (not the row's flags) — `kDestination` (owner death) tears down immediately regardless of subscribers; an empty-flags call is subscriber release, which decrements the count and frees only if the owner already cleared `kDestination`. Owner-death teardown with live subscribers is safe because missiles revalidate the id against the `IdToIndex` map every tick and drop dangling ids without a second `Remove`.
- **Owner-set destination flag**: The owning Spaceship sets the entry's `kDestination` flag after its arrival grace period (documented at the [Collections hub](../CLAUDE.md)). Acquisition and per-tick missile validation both require the flag — missile-side subscription lifetime is documented at [Missiles](../Missiles/CLAUDE.md).
- **Alignment filtering**: Each entry stores an alignment so `Frame::GetMissileTarget` can reject entries that can't collide with the seeker's (same-team rejection).
- **Custom type registration**: Target types push directly into the Interpolate collection's `TypeRegistry`, bypassing the standard `Register()` pathway (the registering method lives on the PostRender struct). The registered visual fields are currently unconsumed — `Render` is a no-op and nothing reads the registry.

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)

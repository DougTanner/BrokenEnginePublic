# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/

Trackable world positions for missile guidance, referenced by stable ID.

## Game-Specific Behavior

- Passive collection: only the mandatory no-op `Update` hooks remain. State mutates through the explicit Add/Remove/AddSubscriber/Sync API invoked by owning Spaceships and homing Missiles — no visual representation or optional merged-phase participation. Unlike its siblings, Targets has no client-only fields and no `BT_CLIENT` code at all.
- Split storage: Interpolate is the `kIdToIndex` side and holds the position that owners drive each tick via `IdToIndex`; PostRender (paired, non-indexable) holds the state tracking whether a target is still alive — id, flags, subscriber count, alignment. Position lives on Interpolate because owners write it during Sync (W forced to 1.0).
- Removal semantics: `Remove` is keyed by the passed flags, not the row's flags. Owner teardown (`kDestination`) removes the row immediately regardless of subscribers. Subscriber release (empty flags) decrements the count, clears the caller's reference, and removes the row only if the destination flag is already clear. Invalid or absent ids are cleared without lookup, so repeated missile lifecycle cleanup is harmless. Owner teardown with live subscribers is safe because missiles revalidate the id against the current paired Targets collections every tick.
- Owner-set destination flag: The owning Spaceship sets the entry's `kDestination` flag after its arrival grace period (documented at the Collections hub). Acquisition and per-tick missile validation both require the flag — missile-side subscription lifetime is documented at `../Missiles/AGENTS.md`.
- Alignment filtering: Each entry stores an alignment so `Frame::GetMissileTarget` can reject entries that can't collide with the seeker's (same-team rejection).

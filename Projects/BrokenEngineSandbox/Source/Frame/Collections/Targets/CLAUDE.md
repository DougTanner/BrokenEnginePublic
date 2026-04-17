# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/

Trackable world positions for missile guidance, referenced by stable ID.

## Game-Specific Behavior

- **Passive collection**: All standard phase methods are intentional no-ops. State mutates only through an explicit Add/Remove/Subscribe/Sync API invoked by owning entities and missiles — no visual representation, no independent phase participation.
- **Indexable by id**: Only sibling `kIdToIndex` collection; owners resolve their target via id each tick to write position. Positions live on Interpolate because owners drive them during Sync.
- **Dual-ownership liveness**: Entry freed only when the owner has cleared its reference AND subscriber count is zero. `Remove` has two modes — owner-death tears down immediately; subscriber release decrements the count.
- **Alignment filtering**: Missile target selection rejects entries whose alignments don't match enemy criteria.
- **Custom type registration**: Types register directly into the interpolate collection's type registry, bypassing the standard `Register()` pathway.

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)

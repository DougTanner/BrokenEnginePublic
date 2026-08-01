---
name: add-collection
description: >-
  Add a dynamically allocated Structure-of-Arrays collection to the engine or
  game frame system. Use for new frame entity, projectile, light, audio, or
  effect types; new Collection<T> structs; FrameBase.h or game Frame.h
  collection registration; and ForEach phase participation. Also use
  proactively whenever implementation creates a struct derived from
  Collection<T>.
allowed-tools: [Read, Edit, Write, Glob, Grep, Bash, PowerShell]
---

# Add a Collection

Add paired Interpolate/PostRender storage without breaking element counts, tuple
order, serialization, deterministic CRCs, save/replay versions, or build
affinity. Follow the live exemplar closest to the requested ownership model;
do not synthesize a collection from a generic full-header template.

## Establish the Variant

1. Read the applicable root, Frame, and Collections `AGENTS.md` files.
2. Decide ownership and reachability before choosing files:
   - game-owned and server-visible;
   - engine-owned and server-visible;
   - engine-owned, whole-file client-only;
   - shared layout with additional client-owned objects.
3. Read every file listed for the selected exemplar. Also inspect its frame
   registration, project membership, and harness query exposure. If the named
   symbols no longer demonstrate the stated variant, stop and report the stale
   guidance instead of substituting another pattern silently.
4. Explicitly invoke `add-collection-member` (`../add-collection-member/SKILL.md`)
   for each new SOA `* __restrict` column and complete its layout checklist.
   This skill owns collection-level wiring; that skill owns every column.

| Requested variant | Live exemplar files and symbols | Pattern to preserve |
|---|---|---|
| Shared-only game collection | `Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/Targets.h`: `TargetsInterpolate`, `TargetsPostRender`; sibling `.cpp` files | `SharedMembers()` plus `Members()` returning it; stable IDs without special frame dispatch |
| Shared game state plus client-owned state | `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.h`: both structs; `Blasters.cpp`: `ClientInit`, `ClientInitAll`, `Transfer`; `BlastersUpdate.cpp` | guarded `ClientMembers()`, client `tuple_cat`, hydration, owned-object teardown, transfer payload |
| Server-visible engine collection | `Engine/Source/Frame/Collections/Pushers/Pushers.h`: both structs; sibling `.cpp` files; `Engine/Source/Frame/FrameBase.h`; `game::Frame::kiVersion` in `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` | direct FrameBase storage, server tuple registration, index helpers, per-struct version terms |
| Owner-synchronized whole-file client-only collection | `Engine/Source/Frame/Collections/Sounds/Sounds.h`: both structs; sibling `.cpp` files; `FrameBase.h` | outer `BT_CLIENT` guard, `Members()` only, owner `Sync`/Add/Remove, custom client-only identity only when required |
| Controller-driven fire-and-forget client-only collection | `Engine/Source/Frame/Collections/Puffs/Puffs.h`: both structs; `Puffs.cpp`, `PuffsUpdate.cpp`, `PuffsRender.cpp`; `FrameBase.h` | `Members()` only, controller metadata copy, paired Add/Destroy, no owner handle or persisted version |

Use the tuple shape that matches reachability:

- Shared-only game: define `SharedMembers()` and return it from `Members()`.
- Shared/client split: define guarded `ClientMembers()`; client `Members()`
  returns `std::tuple_cat(SharedMembers(), ClientMembers())`, while server
  `Members()` returns `SharedMembers()`.
- Server-visible engine collections whose whole layout is shared may expose
  `Members()` only; shared CRC/read helpers fall back to it.
- Pure client-only engine collections expose `Members()` only and guard the
  entire header and implementation.

Keep client-only pointers out of `SharedMembers()`. Keep `SharedMembers()` a
subset of `Members()` and any `SharedCrcMembers()` a subset of
`SharedMembers()`.

## Wire the Pair

Create Interpolate/PostRender structs and their implementation files beside the
chosen exemplar. `Update` is mandatory and must use its generic dispatch
signature. Optional hooks merged into the surrounding phase need no no-op boilerplate: absent
hooks or hooks that cannot be called skip silently, while `Update` and direct/manual calls remain
compile-checked. Declare `extern template struct Collection<...>` for both
structs and explicitly instantiate both in one implementation file.

### Game-owned

- Add forward declarations and paired `std::unique_ptr` members to game
  `Frame.h`.
- Construct both pointers in the matching `Frame.cpp` constructors.
- Include the header and add both objects to `GameInterpolateCollections()` and
  `GamePostRenderCollections()` in `FrameCollections.h`.
- Preserve producer-before-consumer tuple order. A collection that produces
  state consumed later in the same phase must precede that consumer.

Tuple registration supplies normal phase dispatch, allocation/copy walks,
build-local Write/Read, shared ServerRead/CRC, and output merging in
`LogDifferences`.
Reserve explicit per-collection Frame.cpp dispatch for a concrete special
contract such as Players; `CollectionFlags::kIdToIndex` alone is not one.
`Targets` proves that an indexable collection can use the normal tuples.

### Engine-owned

- Include the header in `Engine/Source/Frame/FrameBase.h`.
- Add direct members to both FrameBase structs, entries at matching positions in
  both `Collections()` tuples, and update both `kCollectionCount` values.
- For server-visible state, also add both structs to the corresponding
  `ServerCollections()` tuples. For pure client-only state, guard include,
  members, tuple entries, and client counts with `BT_CLIENT` and omit it from
  `ServerCollections()`.
- Preserve producer-before-consumer order in both tuples.

## Version and Identity Rules

Define `static constexpr int64_t kiVersion` on both structs for every game
collection pair and every server-visible engine pair. Add both terms to
`game::Frame::kiVersion` in `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp`.
Pure client-only engine collections do not alter persisted shared layout and do
not contribute version terms. `PushersInterpolate` and `PushersPostRender` are
the live engine example. The collection-layout auditor (see Verification) checks
declarations and sum terms against each other in both directions.

Use `CollectionFlags::kIdToIndex` only when external owners need a stable
handle. Prefer `AddIndexableElement`, `AddIndexableElementWithId`, and
`RemoveIndexableElement`; they maintain the map during add and swap-and-pop.
Manual identity wiring is conditional on a real alternate ID source or
lifecycle, such as Sounds' client-only UUID stream, not on the flag itself.

## Decisions the Auditor Cannot Make

The collection-layout auditor (see Verification) checks tuple membership, subset
and guard relations, and the version sum. It cannot reach these two decisions,
so settle them before finishing:

- [ ] Transfer and hydration: transferable state has matching send and receive
  wiring; source-owned client objects are removed and destination objects
  recreated. Shared collections with client-owned objects initialize them at
  local spawn and after server state arrives (`ClientInit`/`ClientInitAll`
  pattern).
- [ ] Harness query decision: inspect
  `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServerQueries.cpp` for
  server-visible state. If scenarios need it, add the include, `Extract*`,
  `query_frame` count, `query_collection` arm, and allowed-name error text; if
  not, record the deliberate exclusion. Pure client-only collections have no
  server query.

## Verification

- Resolve every cited path and symbol against the final tree.
- Run the collection-layout auditor (below) and clear every violation.
- Compile every affected client/server target through the repository `compile`
  workflow after the C++ and project-membership stages are complete.
- Run the acceptance scenario required by the task; use the agent harness only
  for runtime-observable criteria.

### Collection-layout auditor

Run from the repository root:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/scripts/Test-CollectionLayout.ps1
```

Sweeps, shell-specific invocation, exit codes, truncation, JSON shape, and the
blocking rule: `../../references/collection-layout-auditor.md`.

## References

- `Engine/Source/Frame/Collections/Collection.h` — shared-member fallbacks,
  serialization/CRC, and indexable helpers
- `Engine/Source/Frame/Collections/AGENTS.md` — generic SOA invariants
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/AGENTS.md` — game
  lifecycle invariants
- `Engine/Source/Frame/AGENTS.md` — tuple ordering and frame serialization

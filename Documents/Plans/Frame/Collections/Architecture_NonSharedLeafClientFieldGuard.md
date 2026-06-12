# Architecture: Guard Against Client-Only Fields on Non-Shared Collection Leaves

## Context

Source: deferred finding from the SharedMembersParityGuard plan (landed-and-removed this session). That plan added a server-build `static_assert(kbServerMembersParity<TStruct>)` plus an `IsMemberTupleSubset` `ASSERT` to `Collection.h` (the `SharedCollectionCrc` / `SharedCollectionRead` helpers, currently ~`Collection.h:634-728`) so that any collection defining `SharedMembers()` is verified to have an identical `Members()` tuple on the server build — closing the wire/CRC shear class for *shared* collections.

The residual gap is structural: the new guard fires **only inside the `if constexpr (HasSharedMembers<TStruct>)` branch**. A collection that defines only `Members()` (no `SharedMembers()`) takes the `else` branch, where `SharedCollectionCrc` and `SharedCollectionRead` walk the full `Members()` tuple. For those collections the parity guard provides zero protection — and zero protection is correct *today*, because a non-shared collection's `Members()` carries no `#if defined(BT_CLIENT)` entries, so client and server walk the same tuple.

The concrete drift surface is `SpaceshipsPostRender` (`Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.h`, the `Members()` accessor ~`:142`). It is the lone asymmetric half of a paired collection: its sibling `SpaceshipsInterpolate` (same file, `SharedMembers()`/`ClientMembers()`/`Members()` at ~`:69-83`) already mixes client-only fields (`pfAnimationTimes`, `puiWindTrails`) via the `SharedMembers`/`ClientMembers` idiom, while `SpaceshipsPostRender` defines only `Members()` (it happens to have no client-only fields today).

The hazard: the natural next edit — adding a `#if defined(BT_CLIENT)` field to `SpaceshipsPostRender::Members()` *without* introducing a `SharedMembers()` split (intuitive, since the Interpolate half already carries client members and the author may not realize the PostRender half lacks the split) — would route that client-only field through the `else` branch, so it would be CRC'd (`SharedCollectionCrc`) and wire-serialized (`SharedCollectionRead`/server broadcast). Result: instant client/server desync at runtime, with **no compile error**. The new parity guard cannot catch it because the collection has no `SharedMembers()` to compare against.

Other non-shared leaves (`Pushers`, `Targets`, `ExplosionsPostRender`) share the same theoretical exposure, but none has an Interpolate sibling already carrying client members, so `SpaceshipsPostRender` is the singular, concrete drift surface and the focus of this plan.

A naive "pairing" `static_assert` (both halves of a pair define `SharedMembers()`, or neither does) would **fire on today's legitimately-asymmetric Spaceships pair** — `SpaceshipsInterpolate` has it, `SpaceshipsPostRender` does not, and that is a correct state. So the fix needs design judgment, not a mechanical assert.

## Design

This is a **decision plan**: present the options below at `/external-grill-plan`, pick one, then execute. Resolution is expected to be option (a) — small and compile-self-verifying — but confirm at grill.

### Candidate resolutions

**(a) Convert `SpaceshipsPostRender` to the `SharedMembers()` idiom now (recommended).**
Give `SpaceshipsPostRender` a `SharedMembers()` that returns its full current set, and define `Members()` in terms of it. This pulls the collection into the `HasSharedMembers` branch so the new parity `static_assert` covers it, and any future client-only field naturally slots into a `ClientMembers()` split rather than the shared `else` path.

Open implementation question to resolve at grill / verify in source before editing: whether an empty `ClientMembers()` + `tuple_cat` is even needed, or whether
```cpp
auto SharedMembers(this auto&& rSelf) { return std::tie(/* all current members */); }
auto Members(this auto&& rSelf) { return rSelf.SharedMembers(); }
```
suffices. The `HasSharedMembers` concept only requires `SharedMembers()` to exist; `kbServerMembersParity` compares `Members()` vs `SharedMembers()` for `std::is_same_v` — with `Members(){ return SharedMembers(); }` they are trivially identical, so no `ClientMembers()` is required while the set has no client-only fields. Confirm `IsMemberTupleSubset(SharedMembers(), Members())` also holds (it must, since they are the same tuple). This is the minimal form; do **not** add an empty `ClientMembers()` unless verification shows a helper requires it.
- Cost: ~one accessor rewrite in `Spaceships.h`, compile-checked both builds, no layout/CRC/`kiVersion` change (the walked tuple is identical).

**(b) Document the rule and rely on review.**
Add to the Collections hub CLAUDE.md (`Engine/Source/Frame/Collections/CLAUDE.md`, Member-tuple-protocol bullet) an explicit rule: *a client-only (`#if defined(BT_CLIENT)`) member may only be added to a collection that defines `SharedMembers()`; never to a collection exposing only `Members()`.* Optionally cross-note it in the game Collections hub. No code change; the `add-collection-member` skill checklist could carry the same caveat.
- Cost: doc-only, zero risk; weaker guarantee (relies on the author reading the rule).

**(c) Compile-time pairing / `BT_CLIENT`-divergence detection.**
Devise a sound static check only if one exists. Note the hard constraints established by the SharedMembersParityGuard work: cross-build (client-vs-server) tuple equality is **not** checkable within a single compilation (each build compiles one side), and type-level tricks were already rejected because element types repeat across members (which is exactly why `IsMemberTupleSubset` compares by address at runtime, not by type). A pairing assert that keys off "sibling Interpolate has `SharedMembers`" would mis-fire on the legitimate Spaceships asymmetry. If no sound compile-time check is found, fall back to (a) or (b). Do not invent a fragile heuristic.

Recommended combination: **(a)** as the concrete fix for the one real drift surface, optionally plus the **(b)** doc rule so the next non-shared leaf is protected by convention. Confirm at grill whether to do both or just (a).

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.h` — `SpaceshipsPostRender::Members()` (~`:142`); `SpaceshipsInterpolate` idiom reference (~`:69-83`)
- `Engine/Source/Frame/Collections/Collection.h` — `HasSharedMembers`, `kbServerMembersParity`, `IsMemberTupleSubset`, `SharedCollectionCrc`, `SharedCollectionRead` (~`:634-728`) — read-only reference for option (a) verification
- `Engine/Source/Frame/Collections/CLAUDE.md` — Member-tuple-protocol bullet (target for option (b) doc rule)

## Out of scope

- The shared-collection parity guard itself (landed via SharedMembersParityGuard, already in `Collection.h`) — this plan does not touch the existing `static_assert`/`ASSERT`.
- Converting `Pushers`, `Targets`, or `ExplosionsPostRender` to the `SharedMembers()` idiom — they have no Interpolate sibling carrying client members, so they are not concrete drift surfaces; revisit only if one later gains client-only fields.
- Adding any actual client-only field to `SpaceshipsPostRender` — there is none today; this plan only makes the structure safe for one.
- Any change to the wire format, CRC value, or `kiVersion` (option (a) walks an identical tuple, so none of these move).

## Acceptance criteria

- After execution, adding a `#if defined(BT_CLIENT)` field to `SpaceshipsPostRender::Members()` without placing it in a client-only split is caught at compile time (option a: the new field would break `kbServerMembersParity` on the server build) or is explicitly prohibited by a documented rule (option b).
- No change to the server-broadcast/CRC tuple walked for `SpaceshipsPostRender` (chosen option must leave the current shared set byte-identical — no `kiVersion` bump, no save/replay invalidation).
- Both client and server builds compile clean.

## Notes

- **Invariant exposure**: option (a) touches a collection's member-tuple protocol — the CRC/serialization path — but the walked tuple stays identical (full set → `SharedMembers()` that returns the same set), so no determinism/CRC *value* change, no `kiVersion`/`.pack` impact, no protocol change. Option (b) is doc-only. Verify the no-change claim by confirming `SpaceshipsPostRender` has zero `#if defined(BT_CLIENT)` members at execution time before relying on it.
- **Pre-staged grill decision**: which of (a) / (b) / (a+b) to land; and for (a), the minimal accessor form (plain `Members(){ return SharedMembers(); }` vs. introducing an empty `ClientMembers()`) — default to the minimal form unless a helper proves to need the split.

## Dependencies

- Follows the **SharedMembersParityGuard** plan (landed-and-removed this session — added `kbServerMembersParity` / `IsMemberTupleSubset` to `Collection.h`). This plan addresses the explicitly-deferred residual gap of that work: the parity guard covers only `HasSharedMembers` collections; this closes the non-shared-leaf exposure it could not reach.

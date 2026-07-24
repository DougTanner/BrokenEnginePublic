<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-05T03:52:38.000Z","dependsOn":[]} -->
# Collection::Read Deserialization Index & Capacity-Stride Hardening

## Context

Surfaced (and extension-review-gated) during the `Save/SaveLoadTrustBoundaryHardening` session (2026-07) and deferred here by user decision — it exceeded that plan's server-only/file-only/no-CRC envelope. Two linked gaps in the shared engine collection deserialization framework `Engine/Source/Frame/Collections/Collection.h`, reached from BOTH the server save/load path (`game::GameSaveLoad::ReadGrid`, `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:543`) AND the client live-network full-state receive (`engine::Client` receive → `game::NetworkSessionContract::ReadFrame`, `Projects/BrokenEngineSandbox/Source/Network/NetworkSessionContract.h:39` → `FrameInterpolateBase`/`FramePostRenderBase::ServerRead`, `Engine/Source/Frame/FrameBase.cpp:59,144` → `SharedCollectionRead`, `Collection.h:400`). `OptionalIdToIndex::Crc` participates in the shared CRC (`SharedCollectionCrc`, `Collection.h:382`).

The two gaps as originally identified:

1. **Unvalidated SOA index in `OptionalIdToIndex<T>::Read`**: a raw `int64_t` index read from the stream and stored with no `[0, iCount)` range check. Consumers subscript SOA arrays through `IdToIndex(id)` (`Collection.h:170-173`; e.g. `puiTargets[...]` in `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.cpp`), so a corrupt save — or hostile/corrupt network full-state — with an in-count map holding an out-of-range value yields an out-of-bounds heap read, and `RemoveIndexableElement` (`Collection.h:551`, `iIndex = idToIndexMap.at(id)` then `SwapElement`) turns the same value into an out-of-bounds **write** at the attacker-controlled index.
2. **Inert byte ceiling at `Collection::Read`**: `ValidateDeserializedCountCapacity(iCount, iCapacity, /*iElementBytes=*/1, …)` reduced the 256 MiB `common::kiMaxDeserializedBytes` ceiling (`Common/Serialization.h:30`) to `iCapacity > 256M/1`, which never fires before the 16M-element cap, letting a hostile stream reach a multi-GB SOA reserve (`iCapacity × Σ(member strides)`).

**Drift status (verified 2026-07-24, current tip):** both gaps are already closed in the current tree. Item 1 is implemented as the bound-threading shape (formerly "Option A"): `OptionalIdToIndex<T>::Read(std::istream&, int64_t iCount)` (`Collection.h:191-240`) validates map cardinality equals `iCount`, every value in `[0, iCount)`, value distinctness via a workbuffer bitmap, and key distinctness via `try_emplace` — a full bijection check, throwing `common::CorruptStreamException` on any violation. Item 2 is enforced downstream where the real stride is known: `AllocateAndAssign` (`Engine/Source/Frame/Collections/CollectionMemory.h:134-168`) computes `MemberTupleBufferSize(iCapacity, members)` and throws `CorruptStreamException` when it exceeds `kiMaxDeserializedBytes` (`CollectionMemory.h:145-149`); the `Collection::Read` call site (`Collection.h:326`) keeps stride 1 deliberately and documents the downstream enforcement. Both failure paths degrade safely: the client drops the packet (`Engine/Source/Network/Client/Client.cpp:290-297`), the server load aborts clean (`ReadGrid` catch, `GameSaveLoad.cpp:606-614`).

**Therefore this plan's implementation work reduces to verification.** The implementer must NOT re-implement, restructure, or "improve" the existing hardening. Execute the steps below; if verification finds every invariant already holding (the expected outcome), make zero code edits and report the plan complete-as-verified.

## Design

Required end-state invariants (all currently believed present — verify, do not rebuild):

1. **Index bijection on read**: `OptionalIdToIndex<T>::Read` receives the already-validated live-row count `iCount` and rejects, via `common::CorruptStreamException`, any stream whose map is not a bijection onto `[0, iCount)`: size mismatch vs `iCount`, value out of `[0, iCount)`, duplicate value, or duplicate key. `Collection::Read` (`Collection.h:316-332`) passes its validated `iCount` into that call.
2. **Effective byte ceiling**: the real per-element SOA byte footprint bounds every deserialization-driven reserve. Enforcement lives in `AllocateAndAssign`, which knows the member tuple: total buffer size above `common::kiMaxDeserializedBytes` throws `CorruptStreamException` before allocation, and failure resets member pointers (`ResetDataToNull`) rather than publishing a torn layout.

Verification steps:

1. Read the named regions and confirm each invariant above against the exact current bytes (close code inspection; no speculative gaps).
2. Run the acceptance criteria below through the agent harness where runtime-observable.
3. If — and only if — a named invariant is found NOT to hold in a named region, restore it with the smallest conforming edit inside that region, matching the surrounding shape (throw `CorruptStreamException`, no new validation layers, no signature changes beyond what the invariant itself requires). Any gap outside the named regions is out of scope: report it, do not fix it.

## Scope contract

The listed scope is both target and ceiling: smallest complete change (expected: none), no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission beyond the named regions plus the mechanical necessities (includes, declarations) a required fix itself demands.

**In scope**

- `Engine/Source/Frame/Collections/Collection.h` — `OptionalIdToIndex<T, FLAGS>::Read` (lines 191-240) and the `Collection<T, FLAGS>::Read` body (lines 316-332) including its `ValidateDeserializedCountCapacity` call: verify (and only on a proven violation, minimally restore) invariant 1 and the documented stride-1 rationale.
- `Engine/Source/Frame/Collections/CollectionMemory.h` — `AllocateAndAssign` (lines 134-168): verify (and only on a proven violation, minimally restore) invariant 2.
- Read-only reference (never edited): `Collection.h` `IdToIndex`/`RemoveIndexableElement`/`SharedCollectionRead`/`SharedCollectionCrc`; `Engine/Source/Frame/FrameBase.cpp` `ServerRead`/CRC entry points; `Engine/Source/Network/Client/Client.cpp` receive catch; `Engine/Source/Network/Client/ClientReceive.cpp` full-state entry; `Projects/BrokenEngineSandbox/Source/Network/NetworkSessionContract.h`; `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp` `ReadGrid`; `Common/Serialization.h` `kiMaxDeserializedBytes`/`ValidateDeserializedCountCapacity`.

**Out of scope**

- Any edit to the two in-scope files outside the named regions, and any edit at all when the invariants verify as holding.
- The server-only file-side gaps already closed by the `SaveLoadTrustBoundaryHardening` session (FrameInput count, `iFlagshipIndex`, `iNextGlobalId`, client-coord, etc.).
- The network StatusChange batch codec (tracked by its own Network plan).
- Wire format, `kiVersion`, `.pack` layout, CRC composition, or serialization-order changes of any kind.

## Critical files

- `Engine/Source/Frame/Collections/Collection.h`
- `Engine/Source/Frame/Collections/CollectionMemory.h`
- Read-only references listed in the scope contract.

## Risk tier and invariant exposure

Tier 3: shared engine collection deserialization on the deterministic reconstruction path — server save/load AND client live-network full-state receive; the ID map participates in the shared CRC. Behavior must remain byte-identical for known-good input; the hardening only converts OOB-read/write/torn outcomes on corrupt or hostile input into uniform `CorruptStreamException` rejection caught on both paths. No wire / `kiVersion` / `.pack` layout change. Because it sits on the deterministic reconstruction path, acceptance needs a replay/CRC soak AND a network full-state soak (agent harness).

## Acceptance criteria

- A corrupt save or full-state carrying an in-count index map with an out-of-range, duplicate-value, or duplicate-key entry → `CorruptStreamException` rejection (server load falls back clean via the `ReadGrid` catch; client drops the packet via the `Client::Receive` catch), never an OOB SOA access.
- A known-good save / replay / network full-state round-trips byte-identically (shared CRC unchanged).
- An oversized-stride reserve (member-tuple buffer size above `kiMaxDeserializedBytes`) is rejected in `AllocateAndAssign` before allocation — the byte ceiling is not inert.

## Coordination

- Never interleave with the live frame-collection series `Documents/Plans/Frame/Architecture_CollectionHelperDedup.md`, `Documents/Plans/Frame/Architecture_PhaseHookOptIn.md`; each later landing must refresh shared collection-header/TU citations (the line numbers in this plan are tip-of-2026-07-24 citations).

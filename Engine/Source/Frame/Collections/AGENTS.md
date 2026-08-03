# Collections - Generic SOA Framework

## Overview

`Collection<T>` provides cache-oriented SOA storage for paired Interpolate and PostRender collection structs. Game and engine collections share this contract; leaves document only behavior-specific deviations.

Use `/add-collection` for a new collection and `/add-collection-member` for any SOA layout change.

## Core Contract

- `Members()` defines allocated and build-local serialized columns. `SharedMembers()` is the server wire/CRC subset; `ClientMembers()` holds client-only state. Server builds require member parity because they write the shared format directly.
- `AllocateAndCopyMembers()` allocates at the previous frame's exact capacity, then copies `PersistentMembers()` when declared or the full `Members()` tuple otherwise. Tuple entries and nested pointer arrays pair by position, so corresponding shapes and element types must match. `PersistentMembers()` must remain a subset of `Members()`; an omitted member's owning phase must initialize it before use.
- Interpolate and PostRender rows have identical counts. Grow/copy/remove them through paired helpers so indices remain aligned.
- A positive capacity always has backing storage, even when `Members()` is empty. Allocation failure must not publish a capacity or layout that was not installed.
- `Update` is mandatory and compile-checked. Other hooks merged into the surrounding phases are opt-in: declare a hook only when it has work, with the exact `ForEach*` signature; hooks that are absent or cannot be called skip silently. `static constexpr bool kbManualRender = true;` opts a collection out of the merge of interpolate and render work, for an explicit manual render path.
- The interpolate-phase `Update` fan-out runs in two situations it cannot tell apart: fixed-step simulation inside the frame tick, and client render interpolation at a variable time step that is 0.0 while a coord's snapshots are still filling. Every `Update` must therefore be correct at any delta time including zero, and must not assume it is called once per tick — no counters, cooldowns, or spawns driven by call count.
- Registering a collection type is a startup-only, once-per-type action with side effects: on the client, a non-zero type CRC schedules that texture chunk to load and registers it for lighting pre-blur. Register every type before the worker threads fan out; afterwards the type registry is read from parallel ticks without synchronization.
- Use `ZeroMemberRow()` when a spawn's defaults are zero across a whole member tuple, then apply collection-specific non-zero defaults. Every Add path must still initialize its complete new row; stale shared state can desynchronize CRCs, and stale client-only state still corrupts local behavior.

## Identity, Serialization, and CRC

- Optional ID maps are part of collection identity. Add/remove/swap operations must keep map entries and row indices synchronized.
- Unconditional removals that consume an owner handle use `RemoveIndexableElementAndClearHandle()`. Conditional lifetime policies clear the handle separately, while internal removals of entries whose IDs were borrowed from another owner keep using `RemoveIndexableElement()`. Generated insertion callables are invoked exactly once so each helper preserves its assigned ID stream.
- Deserialization validates count/capacity relationships, allocates for the incoming layout, and validates ID-map count and indices. Preserve these trust-boundary checks when formats change. Optional `PostRead` hooks run only after all requested local or shared members read successfully, so collection-specific normalization occurs before consumers use the rows.
- The serialized row capacity participates in collection CRCs, so paired growth must remain deterministic for shared collections. The physical buffer capacity actually installed can exceed it after a deserialize reuses a larger existing buffer for fewer rows; that transient capacity drives buffer reuse and post-read zero-fill and stays out of serialization, CRC, and difference logging.
- `SharedMembers()` must be a subset of `Members()`. A narrower `SharedCrcMembers()` must also remain a subset of `SharedMembers()`.
- Never add a `#if defined(BT_CLIENT)` member to a collection that declares only `Members()`. It silently joins the CRC and the server wire format with no compile error, because the parity `static_assert` fires only on collections that declare `SharedMembers()`. Split the leaf into `SharedMembers()` plus `ClientMembers()` first. A leaf with no client-only fields yet can head the trap off by declaring `SharedMembers()` as its full set and forwarding `Members()` to it (`SpaceshipsPostRender`).
- Visual-only spawns take their identity from the separate visual counter: `id_t::GenerateVisual` / `uuid_t::GenerateVisual`, added through `AddVisualIndexableElement`. Using the ordinary counter for a client-only spawn advances it on the client but not the server, so every later server-issued ID differs between the two. Transfer and reconnect re-adds instead use `AddIndexableElementWithId` so the server-issued ID survives. Random-number draws must still happen unconditionally on both builds even when only the client consumes the resulting spawn, or the two random streams drift apart.
- Full build-local reads consume `Members()`; cross-build reads consume only shared columns. Change tuple membership and ordering as one serialization contract.
- `LogDifferences()` compares two independently produced snapshots, and a row-count mismatch between them is itself a reported difference rather than an excluded case. Bound every row loop with `CommonRowCount()` so a loop cannot read past the shorter side's member arrays.

## Runtime Rules

- Main-loop allocation uses paired collection storage or persistent owned state, not transient heap containers. Rendering may publish counters only where that collection actually owns them; counter publication is not a universal collection contract.
- Controlled collections declare persistent controller metadata through `PersistentMembers()`. Audit allocation/copy, spawn, transfer, serialization, and difference logging whenever persistent fields change.
- Client-only graphics resources and render hooks remain under narrow `BT_CLIENT` guards without changing shared layout.
- Client-only state that must survive from one frame to the next without entering the CRC'd frame belongs in file-scope render state keyed by `id_t<T>`, pruned each frame with `EraseStaleRenderState<>()`. That is the sanctioned alternative to adding a client-only column to a shared collection.
- Renderable collections use three phases. `BeginRender` sizes the GPU buffer once, from `AccumulateRenderCapacity()` summed over every active grid cell; `Render` fills it per cell; `EndRender` publishes counters and writes the indirect draw count. Sizing from a single cell's count overflows the buffer as soon as a second cell activates. `kbManualRender` collections opt out and own their own path.

## See Also

- Game collections (`../../../../Projects/BrokenEngineSandbox/Source/Frame/Collections/AGENTS.md`)

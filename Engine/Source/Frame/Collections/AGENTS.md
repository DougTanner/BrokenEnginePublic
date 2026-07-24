# Collections - Generic SOA Framework

## Overview

`Collection<T>` provides cache-oriented SOA storage for paired Interpolate and PostRender collection structs. Game and engine collections share this contract; leaves document only behavior-specific deviations.

Use `/add-collection` for a new collection and `/add-collection-member` for any SOA layout change.

## Core Contract

- `Members()` defines allocated and build-local serialized columns. `SharedMembers()` is the server wire/CRC subset; `ClientMembers()` holds client-only state. Server builds require member parity because they write the shared format directly.
- `AllocateAndCopyMembers()` allocates at the previous frame's exact capacity, then copies `PersistentMembers()` when declared or the full `Members()` tuple otherwise. Tuple entries and nested pointer arrays pair by position, so corresponding shapes and element types must match. `PersistentMembers()` must remain a subset of `Members()`; an omitted member's owning phase must initialize it before use.
- Interpolate and PostRender rows have identical cardinality. Grow/copy/remove them through paired helpers so indices remain aligned.
- A positive capacity always has backing storage, even when `Members()` is empty. Allocation failure must not publish a capacity or layout that was not installed.
- Phase hooks use the generic signatures expected by `ForEach*` dispatch, including unused parameters. Do not add per-collection signature variants.
- Use `ZeroMemberRow()` when a spawn's defaults are zero across a whole member tuple, then apply collection-specific non-zero defaults. Every Add path must still initialize its complete new row; stale shared state can desynchronize CRCs, and stale client-only state still corrupts local behavior.

## Identity, Serialization, and CRC

- Optional ID maps are part of collection identity. Add/remove/swap operations must keep map entries and row indices synchronized.
- Unconditional removals that consume an owner handle use `RemoveIndexableElementAndClearHandle()`. Conditional lifetime policies clear the handle separately, while internal borrowed-ID removals keep using `RemoveIndexableElement()`. Generated insertion callables are invoked exactly once so each helper preserves its assigned ID stream.
- Deserialization validates count/capacity relationships, allocates for the incoming layout, and validates ID-map cardinality and indices. Preserve these trust-boundary checks when formats change.
- The serialized row capacity participates in collection CRCs, so paired growth must remain deterministic for shared collections. The physical buffer capacity actually installed can exceed it after a deserialize reuses a larger existing buffer for fewer rows; that transient capacity drives buffer reuse and post-read zero-fill and stays out of serialization, CRC, and difference logging.
- `SharedMembers()` must be a subset of `Members()`. A narrower `SharedCrcMembers()` must also remain a subset of `SharedMembers()`.
- Full build-local reads consume `Members()`; cross-build reads consume only shared columns. Change tuple membership and ordering as one serialization contract.

## Runtime Rules

- Main-loop allocation uses paired collection storage or persistent owned state, not transient heap containers. Rendering may publish counters only where that collection actually owns them; counter publication is not a universal collection contract.
- Controlled collections declare persistent controller metadata through `PersistentMembers()`. Audit allocation/copy, spawn, transfer, serialization, and difference logging whenever persistent fields change.
- Client-only graphics resources and render hooks remain under narrow `BT_CLIENT` guards without changing shared layout.

## See Also

- [Frame hub](../AGENTS.md)
- [Game collections](../../../../Projects/BrokenEngineSandbox/Source/Frame/Collections/AGENTS.md)

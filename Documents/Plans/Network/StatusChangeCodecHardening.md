# StatusChange Codec Hardening (Send Overflow, Receive Under-Validation, Frame Prefix, Workbuffer Fill)

## Context

Four cross-validated defects in the StatusChange batch codec and full-frame decompress path, all on the network trust boundary. Absorbs the former `Network/StatusChangeBatchSizeValidation.md` (its wire-size clamp is item (c) here). The codec is declared `engine::` but implemented in the game layer (`Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp`); the full-frame reader is engine client code.

### (a) Silent send-side batch overflow → unlogged desync

`CompressStatusChangeBatch` (`NetworkSerialization.cpp:352`) writes the 4-byte uncompressed-size prefix, then calls `LZ4_compress_default` into `pDest` and returns `sizeof(int32_t) + iCompressedSize` **without checking the compress result**. `LZ4_compress_default` returns 0 when the destination is too small — the function then returns a 4-byte prefix-only payload. `Server::BufferFrame` (`Engine/Source/Network/Server/Server.cpp:246`) compresses into `mCompressionBuffer`, sized `kiMaxPacketSize` (`NetworkProtocol.h:75`, 64 KB). Worst-case serialized batch is `kiMaxStatusChangesPerCell`(1024) × `kiMaxBytesPerItem`(120) + group headers ≈ 123 KB — nearly double the dest. On overflow the client decodes 0 changes against a still-valid `sharedCrc`, so every subscriber to that coord desyncs with **zero server-side log**. Nothing send-side enforces the 1024/cell cap either; the client silently truncates at `kiMaxStatusChangesPerCell` in `DecompressStatusChangeBatch`→`DeserializeStatusChangeBatch` (`ClientReceive.cpp:344`). Reachable in normal play via a mass fleet-transfer burst crossing a cell boundary (spawns + transfers concatenated in `ServerBroadcaster::BroadcastTick`, `ServerBroadcaster.cpp:120-141`).

### (b) `DeserializeStatusChangeBatch` receive-side under-validation (`NetworkSerialization.cpp:269-350`)

Three sub-defects on decompressed wire data:
1. **Per-item over-read.** The inner loop guards only `pCursor < pEnd` (`:285`), then a single item can read up to ~112 bytes (`DeserializePlayerTransfer`, the largest payload) past `pEnd`. The game Network `CLAUDE.md` claim that int64-chunk rounding "provides slack" covers only ≤7 bytes (the `iChunks*8` rounding in `DecompressStatusChangeBatch:403`), far short of a full player-transfer read.
2. **Unvalidated wire type byte.** `ReadUint8` is cast straight to `game::StatusChangeType` (`:282`) with no check against `game::StatusChangeType::kCount`. An unknown type yields `DefaultDataForType` defaults, the `switch` matches no case so the cursor never advances, and up to `uiGroupCount` (uint16, ≤65535, wire-controlled — `:283`) garbage `StatusChange`s are emitted into `pDest` for downstream frame code to `switch` on.
3. **Late truncation check.** The post-group `if (pCursor > pEnd)` (`:342`) fires only *after* partially-read garbage items were already counted into `iOutputCount`, so a truncated final group still contributes bogus output.

### (c) Hostile full-frame size prefix (absorbs `StatusChangeBatchSizeValidation.md`)

Two sites trust a wire-controlled uncompressed size up to `INT32_MAX` before LZ4 bounds anything:
- `DecompressAndReadFrame` (`Engine/Source/Network/Client/ClientReceive.cpp:14`): reads `iUncompressedSize` (`:21`), rejects `<= 0` (`:24`) but not an upper bound, then `std::string decompressed(iUncompressedSize, '\0')` (`:30`) allocates up to ~2 GB before `LZ4_decompress_safe` fails.
- `DecompressStatusChangeBatch` (`NetworkSerialization.cpp:387`): the `iUncompressedSize` prefix (`:396`) drives the workbuffer push loop `iChunks = (iUncompressedSize + 7) / 8` (`:403-407`) — up to ~2 GB of thread-workbuffer growth — *before* `LZ4_decompress_safe`. This is the exact clamp the absorbed plan specified.

### (d) Minor rider: element-wise workbuffer zero-fill

`SerializeStatusChangeBatch` (`:249-252`), `CompressStatusChangeBatch` (`:367-371`), and `DecompressStatusChangeBatch` (`:403-407`) reserve workbuffer scratch via individual `PushBack<int64_t>(0)` calls in a loop — up to ~15 k iterations per packet at worst case (`~123 KB / 8`).

## Design

- **(a)** In `CompressStatusChangeBatch`: capture the `LZ4_compress_default` return; if `<= 0`, `LOG(kNetwork, kError, ...)` (coord/count/serialized-size) and return 0 (drop, consistent with the decompress-failure convention). Size the compress destination against `LZ4_compressBound(iMaxSerializedSize)` rather than assuming `kiMaxPacketSize` fits — either widen the scratch/`mCompressionBuffer` path (reuse the existing `Server::CompressToBuffer` bound-and-grow pattern at `Server.cpp:330`) or reject over-cap batches before compressing. In `Server::BufferFrame` (or its caller `ServerBroadcaster::BroadcastStatusChanges`), enforce the per-cell cap: `ASSERT` + `LOG(kNetwork, kError, ...)` when `statusChanges.size() > kiMaxStatusChangesPerCell`, so a send-side overflow is loud instead of a silent client truncation.
- **(b)** Add a per-type serialized-size table (one entry per `game::StatusChangeType`, mirroring the `Serialize*Transfer`/`SerializeGroup` write widths) and drive per-item reads through `engine::BoundedCursor` (`NetworkCursor.h`): call `Has(typeWireSize(eType))` before each item and stop the group cleanly when it fails. Reject an unknown/out-of-range type byte (`>= kCount`) — abandon the group (or the whole batch) rather than emitting defaulted garbage. On truncation, roll `iOutputCount` back to the group's start index (captured before the inner loop) so no partially-read item survives; equivalently, reject the whole batch. Keep the existing `kVerbose` truncation log.
- **(c)** Clamp both `iUncompressedSize` reads before allocating: against an explicit max-uncompressed constant (new `inline constexpr` in `NetworkProtocol.h`, e.g. `kiMaxUncompressedFrameBytes` / `kiMaxUncompressedStatusBatchBytes`) or `iCompressedSize * 255` (LZ4's worst-case expansion ratio). For the StatusChange path reuse the absorbed plan's bound: `iMaxCount * sizeof(game::StatusChange)` (caller passes `iMaxCount ≤ 1024`). On violation, take the existing failure return (0 / `nullptr`).
- **(d)** Add a bulk zero-extend to `common::Workbuffer` (a `PushZeroed(iBytes)` / `PushBackN<int64_t>(iCount)` that grows once and zero-fills) and replace the three push loops with a single call each. Scope it to the workbuffer API + these three call sites.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` — `CompressStatusChangeBatch`, `DeserializeStatusChangeBatch`, `DecompressStatusChangeBatch`, `SerializeStatusChangeBatch`; new per-type wire-size table.
- `Engine/Source/Network/Server/Server.cpp` — `Server::BufferFrame` (count-cap enforcement; compress-dest sizing).
- `Engine/Source/Network/Client/ClientReceive.cpp` — `DecompressAndReadFrame` (full-frame prefix clamp).
- `Engine/Source/Network/NetworkProtocol.h` — new max-uncompressed constant(s).
- `Engine/Source/Network/NetworkCursor.h` — `BoundedCursor` is the receive-side validation primitive (consumed, not modified).
- `Common/` Workbuffer — new bulk zero-extend member (item (d)).
- `Projects/BrokenEngineSandbox/Source/Network/CLAUDE.md` — update the "compression result is unchecked" line and the "int64-chunk-rounded workbuffer scratch provides it" slack rationale once fixed.

## Acceptance criteria

- A batch exceeding the compress destination logs `kError` server-side and drops the coord update rather than shipping a 4-byte prefix that silently zeroes the client's changes.
- A per-cell status-change count over `kiMaxStatusChangesPerCell` is caught send-side (ASSERT + `kError`), not silently truncated on the client.
- `DeserializeStatusChangeBatch` never reads past `pEnd`, never emits an item for an out-of-range type byte, and never counts a partially-read (truncated) item into its return.
- Neither `DecompressAndReadFrame` nor `DecompressStatusChangeBatch` allocates more than the clamped maximum for a hostile size prefix.
- The three workbuffer reserve loops are single bulk calls.

## Out of scope

- Send/receive layout pairing restructure — live plan `Network/Architecture_WireFormatPairing.md`.
- The `SerializeGroup` `kiMaxBytesPerItem` ASSERT and the LZ4 envelope wire format itself (unchanged; no version bump).
- Reconciliation / ACK / resend behavior — the fix only changes rejection paths, not accepted-packet semantics.
- Adding new `StatusChangeType`s or altering existing payloads.

## Notes

- **Invariant exposure**: no wire change, no `kiVersion`/`.pack`/CRC/determinism exposure — malformed packets are already rejected downstream; this makes rejection cheap, bounded, and *logged*. Touches network trust-boundary receive paths and allocation-tracked workbuffer paths (item (d) must stay allocation-tracker clean — a single grow, no per-frame heap in the main loop; float-free `LOG` formatting per repo rules).
- **Grill decisions**: (1) item (b) truncation/unknown-type handling — roll `iOutputCount` back to group start **or** reject the whole batch (recommend group-level rollback to preserve valid earlier groups). (2) item (c) clamp basis — explicit max constant **or** `iCompressedSize * 255` (recommend explicit named constant for readability). (3) item (a) over-cap handling — reject-and-log **or** widen dest to `LZ4_compressBound` and keep sending (recommend both: bound the dest so legitimate large-but-valid batches ship, and cap+log the pathological count).
- Absorbs and replaces `Network/StatusChangeBatchSizeValidation.md` (deleted with this plan; orchestrator removes its `Order.md` row and its mention in the `## Standalone` Dependencies bullet).

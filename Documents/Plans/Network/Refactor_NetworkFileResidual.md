# Refactor: Network/File Residual Quick Wins

## Context
Source: /external-refactor-clean on Engine/Source (recursive). The engine Network directory has ~20 live plans and the File side 4 — this is the short residual list those plans don't own. Several items must sequence around the live refactors that share these files.

## Design

### Engine/Source/File/FileManager.h
- Change `WriteVersionedFile`'s `STRUCT_TYPE& rStructure` (:300) to `const STRUCT_TYPE&` — the body only writes (the `has_binary_stream_operators` trait at :238 requires const-compatible `operator<<`; `common::Write` takes const). The non-const signature forces a documented defensive copy at `ClientSessionBase.cpp:36-37` — delete the copy + comment, pass `rGuid` directly. `ClientSessionBase.cpp` is slated for deletion by `Refactor_SessionBaseCollapse` — land whichever comes first; the other adjusts trivially [~15m]

### Engine/Source/File/DifferenceStream.h
- Make `DifferenceStreamWriter::miStartTick` (:140) a ctor local — assigned at :29, read only in the ctor LOGs at :33 and :38, no reference outside the ctor (the *reader*'s `miStartTick` is genuinely used, `:285`) [~5m]

### Engine/Source/Network/Server/ServerReceive.cpp
- Dedupe the slot-validation predicate in `ClientAckStream` — the accept branch (:48-51) and the epoch-mismatch log branch (:69-71) both re-evaluate `uiSlotIndex < std::ssize(...) && (flags & kActive)`; restructure so the index/active check happens once. **Fold into or co-schedule with `Architecture_WireFormatPairing`** (it restructures this function's read layout); do not interleave [~15m]

### Engine/Source/Network/Server/ServerSend.cpp
- Replace the three copies of `if (prevResendCounts.at(iSlot) != 0) { ... = 0; }` in `SendResends`' early-continue paths (:169-172, :179-182, :196-199) with `UpdateResendLogState(rClient, iSlot, 0, coord)` (:242) — the direct resets bypass the helper, so a slot that stops resending via those paths never emits the was-resending→resolved transition log (a hole in the documented delta-only resend-logging invariant). **Land before `Refactor_ServerClientPlayerRegistry`** (it merges these fields into `SlotState`) [~15m]

### Riders recorded for existing plans (not items here)
- `ClientSessionBase.cpp:314` always-true `miLatestServerTick >= 0` guard around the :316 LOG — fold the removal into `ClockErrorMachineryCleanup`'s edit of the same reset site
- `DifferenceStreamReader`'s ~115-line ctor (:152-266) — cold server-only path with correct trust-boundary validation; decomposition available but low-value; deliberately deferred

## Critical files
- `Engine/Source/File/FileManager.h`, `DifferenceStream.h`
- `Engine/Source/Network/Server/ServerReceive.cpp`, `ServerSend.cpp`
- `Engine/Source/Network/Client/ClientSessionBase.cpp` (copy deletion)

## Out of scope
- Everything owned by the ~24 live Network/File plans (drain naming, wire pairing, session collapse, trust hardening, codec hardening, FileManager split, etc.)
- `FileManager.cpp` (over the 1000-line threshold; `FileManagerReduceFile.md` owns it)

## Notes
- Invariant exposure: none — no wire/CRC/determinism change; the `SendResends` fix alters only verbose-log emission timing. Sequencing constraints above are the plan's main content — respect the Order.md session-refactor and registry ordering
- No open decisions

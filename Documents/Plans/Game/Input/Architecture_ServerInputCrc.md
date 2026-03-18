# Architecture: ServerInputCrc Field Sync Risk

Source: /external-architecture-review on Projects/BrokenEngineSandbox/Source/Input

## Changes

### Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h
- Lines 39-68: `TransferData::operator==()` manually enumerates 23 shared fields plus 1 client-only field (`smokeTrailId`). This list must stay in sync with `FrameInput::ServerInputCrc()` in Input.cpp. Add a `SharedMembers()` method to TransferData that returns `std::tie(vecPosition, vecDirection, vecVelocity, alignment, fHealth, fShield, ...)` for all shared fields (excluding client-only `smokeTrailId`), following the same pattern used by Collection types (Players, Blasters, etc.) [~30m]
- Rewrite `operator==()` to use `SharedMembers()` comparison plus the `#ifdef BT_CLIENT` `smokeTrailId` check

### Projects/BrokenEngineSandbox/Source/Input/Input.cpp
- Lines 123-154: Rewrite `ServerInputCrc()` to iterate over `TransferData::SharedMembers()` tuple via `std::apply` with a CRC fold, eliminating the manual field list. This ensures any field added to `SharedMembers()` is automatically included in the CRC [~15m]

### Common utilities (if needed)
- Add a `common::CrcTuple()` helper that folds `common::Crc()` over a `std::tuple` via `std::apply`, or verify the existing `common::Crc()` already handles tuples [~15m]

## Verification Notes
- Field lists confirmed identical across both locations (23 shared fields + 1 client-only). Sync risk is real. Caveat: `XMVECTOR` alignment (`__m128`) may cause issues with `std::tie`; may need `std::make_tuple` (copies) instead, or a custom reflection approach that avoids tuples for aligned types.

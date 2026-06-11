# NetworkSerialization: Stale kTransferPlayer Byte-Count in Comment + static_assert

## Context

`CompressStatusChangeBatch` (`Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp:346-358`)
sizes its serialization scratch from a per-item upper bound, guarded by a `static_assert` against the largest
`StatusChange` payload (`kTransferPlayer`):

```cpp
// Largest type is kTransferPlayer: 3 Vec4(16) + uint32(4) + 11 float(4) + uint8(1) + int64(8) + GridCoord(8) = 113 bytes
constexpr int64_t kiMaxBytesPerItem = 120;
static_assert(kiMaxBytesPerItem >= 113, "kiMaxBytesPerItem must cover the largest StatusChange serialization (currently kTransferPlayer at 113 bytes)");
```

The "113" is **stale**. Counting the actual `SerializeTransferPlayer` writer
(`NetworkSerialization.cpp:55-75`), the `kTransferPlayer` payload is larger:

- `WriteVec4 × 3` (vecPosition, vecDirection, vecVelocity) = 48
- `WriteUint32` (alignment) = 4
- `WriteFloat × 11` (fHealth, fShield, fNextBlasterFireTime, fNextSecondarySpawnTime, fShieldCooldown,
  fShieldDownSoundCooldown, fAnimationTime, fShieldRotation, fShieldShrink, fArrivalGracePeriod,
  fNavigationDelay) = 44
- `WriteUint16` (uiPlayerFlags, `:69`) = 2
- `WriteInt64` (globalPlayerId, `:72`) = 8
- `WriteGridCoord` (fleetWantedCoord, `:73`) = 8
- `WriteUint8 × 2` (uiPendingFleetWantedCoordTicks, uiPendingWeaponModeTicks, `:74-75`) = 2

Total = **116 bytes** (the comment's tally omits the `uint16 uiPlayerFlags` and one of the trailing `uint8`
countdown fields, and is written as "11 float / uint8(1)" — it predates fields added later).

**No buffer overflow today**: `kiMaxBytesPerItem = 120 ≥ 116`, so the scratch is still big enough and the
`static_assert` still passes. The defect is that the assert's *guard number* (and the comment) describe a
payload that no longer exists — the assert reads `>= 113` for a payload that is actually 116, so it would only
catch a regression once a payload exceeded 120, not once it exceeded the true current size. If a future field
push the real payload past 120 while someone "fixed" the comment to a wrong number, the guard's intent is
muddied. This is a documentation/guard-accuracy bug, not a live overflow.

## Design

Make the comment and the `static_assert` reflect the real serialized size, and (optionally) make the guard
self-maintaining so it cannot drift again:

1. **Correct the byte tally** in the comment at `:354` to the actual field list and total (116): `3 Vec4(48)
   + uint32(4) + 11 float(44) + uint16(2) + int64(8) + GridCoord(8) + 2 uint8(2) = 116`.
2. **Correct the `static_assert` number** at `:356` from `113` to `116` (both the comparison literal and the
   message). Keep `kiMaxBytesPerItem = 120` (it has 4 bytes of headroom over the true 116 — fine).
3. **Optional hardening (recommended, removes the drift class):** instead of a hand-counted magic number,
   derive the guard from the serializer itself. The cleanest mirror of the existing layout-lock pattern (cf.
   `DataFile.h`'s `sizeof`/`offsetof` `static_assert`s) is to compute the worst-case `kTransferPlayer` size
   from `sizeof` of its writer's fields, or to add a single source-of-truth `constexpr` per-type max next to
   `SerializeTransferPlayer` that both the writer's bound and this assert reference. If a low-effort derivation
   isn't clean (the writer uses `Write*` helpers, not a packed struct), at minimum add a comment pointing at
   `SerializeTransferPlayer` so the next field addition knows to re-tally here. The `Network/CLAUDE.md`
   "Adding a StatusChangeType" / "StatusChange Batch Format" sections already say to re-check
   `kiMaxBytesPerItem` on payload growth — this plan makes the number that check compares against honest.

Grill decision: just fix the number (steps 1-2) vs also add the self-maintaining derivation (step 3). Lean
toward 1-2 plus a pointer-comment if step 3's derivation isn't trivially clean, since the writer is
helper-based rather than a single struct.

## Out of scope

- The actual serialization/deserialization of `kTransferPlayer` and the other StatusChange types — the wire
  format is correct; only the *guard's documentation/number* is wrong.
- Changing `kiMaxBytesPerItem` (120) — it still covers the real 116 with headroom; no need to change it (and
  changing it would shift scratch sizing for no benefit).
- The LZ4 envelope, workbuffer scratch sizing math (`:357-364`), and the group-header bound
  (`kiMaxGroupHeaders`) — unchanged.
- Any `kiVersion` / wire-format change — this is comment + assert accuracy only.
- Re-tallying the *other* StatusChange types' sizes — only `kTransferPlayer` (the documented "largest") is the
  guard's subject.

## Acceptance criteria

- The comment at `:354` and the `static_assert` at `:356` state the **actual** `kTransferPlayer` serialized
  size (116), matching `SerializeTransferPlayer`.
- `kiMaxBytesPerItem` still `static_assert`s as covering the largest payload (`120 >= 116` passes).
- (If step 3 is taken) the guard derives from or is comment-linked to `SerializeTransferPlayer` so a future
  field addition is forced to update it.
- Client and server build clean (the assert is `constexpr`, evaluated in both builds).

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` — the comment (`:354`) and
  `static_assert` (`:356`) in `CompressStatusChangeBatch`; `SerializeTransferPlayer` (`:55-75`) is the source
  of truth for the real byte count and the field list to re-tally against.
- `Projects/BrokenEngineSandbox/Source/Network/CLAUDE.md` — "StatusChange Batch Format" / "Adding a
  StatusChangeType" already reference the `kiMaxBytesPerItem` re-check; no change needed unless step 3 changes
  the mechanism.

## Notes

- No live overflow — purely a stale guard number + comment. Low impact, near-zero risk (the assert still
  passes; this only makes its intent honest).
- Sibling theme to the `DataFile.h` layout-lock `static_assert`s that landed in the determinism-bytes session —
  same "lock the number to the layout so it can't drift" idea, applied to a wire-payload size guard.

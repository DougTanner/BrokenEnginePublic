# Frame Version Gate Completeness (Pushers/Explosions kiVersion + IsTransferType Guard)

## Context

`Frame::kiVersion` (`Frame.cpp:11`) claims to sum "every collection's kiVersion" (Frame/CLAUDE.md), but the 2026-07-03 review sweep confirmed it omits three CRC'd, serialized engine collections: it includes `engine::ExplosionsInterpolate::kiVersion` only, while the engine base's cross-build-shared subset (`ServerCollections()`, `FrameBase.h:119/230`) is `{explosions, pushers}` — both `Write()`/`Read()`-serialized and CRC'd. `PushersInterpolate`/`PushersPostRender` (`Pushers.h:34/90`) define **no `kiVersion` at all**, and `ExplosionsPostRender` has none either (only `ExplosionsInterpolate`, `Explosions.h:107`). Failure scenario: an SOA member added to Pushers (or ExplosionsPostRender) changes the save/replay layout with no version bump — old saves/replays deserialize as shifted garbage instead of being rejected by the gate.

Related one-liner from the same sweep: `IsTransferType` (`StatusChange.h:21-24`) requires `kTransferPlayer..kTransferMissile` to stay contiguous, and the serialized `uint8_t` enum values are wire/save bytes — but nothing in the header (no `static_assert`, no comment) guards either property; the reliance is documented only in Frame/CLAUDE.md.

## Design

1. Add `kiVersion` constants to `PushersInterpolate`, `PushersPostRender`, and `ExplosionsPostRender`, following the existing `ExplosionsInterpolate::kiVersion` pattern (`Explosions.h:107`), with the standard bump-on-layout-change comment.
2. Fold all three into the `Frame::kiVersion` sum in `Frame.cpp:11` (making the Frame/CLAUDE.md wording true).
3. Add a `static_assert` in `StatusChange.h` pinning the `kTransferPlayer..kTransferMissile` enumerator arithmetic (contiguity + count), plus a one-line append-only comment on `StatusChangeType` noting the enum values are wire/save bytes (the gap `Network/DeadMachinerySweep.md` Notes documents).

## Critical files

- `Engine/Source/Frame/Collections/Pushers/Pushers.h` — new `kiVersion` on both structs
- `Engine/Source/Frame/Collections/Explosions/Explosions.h` — new `ExplosionsPostRender::kiVersion`
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` — the `Frame::kiVersion` composition
- `Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h` — `IsTransferType` `static_assert` + enum comment

## Out of scope

- The 115→116 base bump for the landed CRC-mixing change — owned by `Frame/CrcVersionGateBump.md` (same line of `Frame.cpp`; co-schedule).
- Any actual layout change to Pushers/Explosions.
- Making `Frame::kiVersion` incorporate `StatusChangeType` itself — `DeadMachinerySweep.md` documents why the reserved-enumerator convention is the chosen mitigation; the comment added here encodes it at the enum.

## Acceptance criteria

- Every collection in the engine `ServerCollections()` shared subset and every game collection contributes a `kiVersion` term to `Frame::kiVersion`.
- Inserting an enumerator between the transfer types fails to compile.

## Notes

- **Invariant exposure.** Adding terms changes the `Frame::kiVersion` value → deliberately invalidates all pre-change saves/replays once (same class of event as `CrcVersionGateBump`). No CRC math, wire layout, or sim behavior changes; no client/server guard interaction. Determinism untouched.
- No open decisions — initial constant values are trivial choices.
- Co-schedule with `Frame/CrcVersionGateBump.md` (both edit the `Frame.cpp:11` composition — one combined bump event, one replay invalidation instead of two) and refresh its citation afterward. The `StatusChange.h` edit shares the file with the transfer trio (see Order.md Dependencies) — line-drift only.

# Refactor: Extract ClientReceive Epoch State Machine

Source: /external-refactor-clean on Engine/Source/Network/Client/ClientReceive.cpp

Two dense 55-60-line handlers contain an implicit state machine over `(current subscription state, epoch, coord)`. Extracting the classification logic makes the branches explicit and the happy path easy to follow.

## Changes

### Engine/Source/Network/Client/Client.h
- Add private enum and helpers:
  ```cpp
  enum class FullStateDisposition : uint8_t { kAccept, kReject, kRejectWithUnsubscribe };
  FullStateDisposition ClassifyFullState(uint8_t uiSlotIndex, uint16_t uiEpoch, GridCoord coord);
  bool TryEpochHeal(ClientCoordSlot& rSlot, uint8_t uiSlotIndex, uint16_t uiEpoch, GridCoord coord);
  void RejectGhostSlot(uint8_t uiSlotIndex, GridCoord coord);
  void CommitSubscribeAccept(ClientCoordSlot& rSlot, uint8_t uiSlotIndex, uint16_t uiEpoch, GridCoord coord);
  ```
- Scope helpers `private:`; they're only called from `ClientReceive.cpp`. [~15m]

### Engine/Source/Network/Client/ClientReceive.cpp
- `ServerCoordFullState` handler (lines 96-155): extract the decision block at 101-139 into `ClassifyFullState`. The handler becomes: decompress → classify → switch on disposition (`Accept`, `Reject`, `RejectWithUnsubscribe`). Target: 60 → ~35 lines. [~45m]
- `ServerSubscribeAccept` handler (lines 389-445): extract three named helpers — `TryEpochHeal` (404-416), `RejectGhostSlot` (417-423), `CommitSubscribeAccept` (431-444). Handler becomes a flat dispatcher. Target: 55 → ~20 lines. [~45m]

## Verification
- Rebuild client config; run a subscription-heavy session (rapid subscribe/unsubscribe of different coords) and watch for `LOG(kNetwork, kWarning, ...)` divergences vs. pre-refactor.
- Specifically test the out-of-order case: full-state arriving before subscribe-accept (documented in `Engine/Source/Network/CLAUDE.md`). The `kUnsubscribed` branch in `ClassifyFullState` must still find the placeholder via `FindSlotForPlaceholder`.

## Verification Notes
Verified — `ClientReceive.cpp:96-155` is `ServerCoordFullState`, `:389-445` is `ServerSubscribeAccept`; line counts (60 and 55) and sub-block ranges match commit d08678d3. Extraction is behavior-preserving and aligns with the out-of-order invariant in `Engine/Source/Network/Client/CLAUDE.md` ("full-state can arrive before subscribe-accept").

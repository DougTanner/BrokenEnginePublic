# Tech Debt: Code Duplication

Source: /external-tech-debt on Engine/Source/Network/Client

## Changes

### Engine/Source/Network/Client/ClientSend.cpp
- Deduplicate `SendUnsubscribe` (line 216) and `SendUnsubscribeOnly` (line 244): the packet-building code is identical. Refactor `SendUnsubscribe` to set `mCoordSlots.at(iSlot).eState = CoordSubscriptionState::kUnsubscribing` then call `SendUnsubscribeOnly(iSlot)` [~5m]

### Engine/Source/Network/Client/ClientReceive.cpp
- Extract a `RemoveCancelledSubscription(GridCoord coord)` helper that does `std::ranges::find(mCancelledSubscriptions, coord)` + `erase` and returns bool. Replace the 4 duplicated instances at lines 101-105, 124-128, 319-323, and 343-347 [~10m]

## Verification Notes
- The cancelled subscription pattern has 4 instances (not 3 as originally counted) — the 4th is in `ServerSubscribeAccept` at lines 343-347

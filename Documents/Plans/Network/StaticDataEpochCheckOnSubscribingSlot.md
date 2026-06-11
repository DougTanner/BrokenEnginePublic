# ServerCoordStaticData Applies Epoch Check to kSubscribing Slots (vs Documented Skip and Sibling Handlers)

## Context

`Engine/Source/Network/Client/CLAUDE.md` ("Subscription Receive Invariants") states the epoch-check rule:

> **Epoch check** (drop rationale: hub's slot ACK model) applies **only where the slot has a server-assigned
> epoch** — a `kSubscribing` placeholder has none yet.

The classify helpers in `Engine/Source/Network/Client/ClientReceive.cpp` honor this:

- `ClassifyFullState` gates the epoch check to `kWaitingFullState` **only**:
  `if (rSlot.eState == CoordSubscriptionState::kWaitingFullState && uiEpoch != rSlot.ackState.uiEpoch) return {};`
  — a `kSubscribing` placeholder skips the epoch check (it's matched on coord identity instead).
- `ClassifyCoordUpdate` applies the epoch check only to `kWaitingFullState` / `kActive` (`uiEpoch ==
  rSlot.ackState.uiEpoch`), never `kSubscribing`.

But `Client::ServerCoordStaticData` (`ClientReceive.cpp:251`) applies the epoch check to **both**
`kWaitingFullState` *and* `kSubscribing`:

```cpp
if ((rSlot.eState == CoordSubscriptionState::kWaitingFullState || rSlot.eState == CoordSubscriptionState::kSubscribing)
    && uiEpoch != rSlot.ackState.uiEpoch)
{
    return;
}
```

A `kSubscribing` slot's `ackState.uiEpoch` was **never reset by `SendSubscribe`** (it has no server-assigned
epoch yet — the doc's whole point), so this comparison tests an incoming `uiEpoch` against a stale/leftover slot
epoch. So `ServerCoordStaticData` diverges from its two sibling classify handlers **and** from the documented
rule: it drops (or admits) static-data packets for `kSubscribing` slots based on an epoch the slot doesn't
legitimately hold.

Possible effects (to determine): for a `kSubscribing` slot, static data arriving before the subscribe-accept
sets the epoch could be wrongly dropped (if leftover epoch ≠ incoming) — losing the once-per-subscription NavData
payload — or wrongly admitted (if leftover epoch coincidentally matches). Static data is sent once per
subscription and carries NavData, so a wrong drop is not free.

This plan resolves the doc/code intent: either the static-data handler **should** skip the epoch check for
`kSubscribing` (match the siblings + doc → remove `kSubscribing` from the epoch-check condition), or there is a
deliberate reason static data is stricter than full-state/coord-update (→ document the exception and confirm the
slot epoch is meaningfully set for `kSubscribing` in this path).

## Design

1. **Determine the intended behavior for `kSubscribing` static data.** Trace the lifecycle: when does a
   `kSubscribing` slot's `ackState.uiEpoch` get a value? `ServerSubscribeAccept` sets `rSlot.ackState.uiEpoch =
   uiEpoch` when it commits/heals; before that, a freshly-`kSubscribing` placeholder's epoch is whatever the slot
   held (0 on a fresh slot, or a stale predecessor's epoch on a recycled slot). Confirm whether static data can
   legitimately arrive while a slot is `kSubscribing` (full state and subscribe-accept "can arrive in either
   order across channels" per the hub doc — static data is a third once-per-subscription message, so it can
   plausibly race ahead of the accept too).
2. **Compare to the siblings.** `ClassifyFullState` handles the analogous race by matching `kSubscribing` on
   **coord identity** (`rSlot.coord != coord → kRejectAsGhost`) and skipping the epoch check; the natural
   consistent fix is for `ServerCoordStaticData` to do the same — drop the `|| kSubscribing` from the epoch
   condition (it already gates the *acceptance* on the `kWaitingFullState || kSubscribing || kUnsubscribed`
   state check above, and could add the same coord-identity match the full-state path uses if ghost protection is
   needed for `kSubscribing` static data).
3. **Resolve:**
   - **Option A — match siblings + doc (recommended pending step 1):** remove `kSubscribing` from the epoch-check
     condition so a `kSubscribing` placeholder skips the epoch check (it has no server-assigned epoch); rely on
     the existing state gate (and add a coord-identity match if a `kSubscribing` static-data ghost is reachable,
     mirroring `ClassifyFullState`).
   - **Option B — deliberate stricter rule:** if static data must be epoch-gated even for `kSubscribing` (e.g.
     because `SendSubscribe` *should* seed the epoch and the bug is the missing reset), document the exception in
     `Client/CLAUDE.md` and ensure the slot epoch is correctly set before this check — then the doc's "applies
     only where the slot has a server-assigned epoch" needs a carve-out for the static-data path.
   - **Recommendation: A**, unless step 1 shows static data genuinely needs epoch gating for `kSubscribing` —
     consistency with the two sibling classify helpers and the documented rule is the simplest correct state.

Verify root cause before editing (Diagnosis Discipline): confirm via the slot lifecycle that a `kSubscribing`
slot's `ackState.uiEpoch` is indeed not server-assigned at the point `ServerCoordStaticData` reads it (the doc
asserts this; confirm `SendSubscribe` / placeholder creation doesn't set it).

## Out of scope

- **`ClassifyFullState` / `ClassifyCoordUpdate`** — already follow the documented rule; unchanged (they are the
  reference behavior this plan aligns the static-data handler to).
- **The epoch / ACK model itself** (`AckState`, epoch mismatch drop) — unchanged; this is about *which states*
  the static-data handler applies the existing check to.
- **The `kWaitingFullState` / `kUnsubscribed` branches** of `ServerCoordStaticData`'s state gate — only the
  `kSubscribing` epoch-check participation is in question.
- **The static-data payload / NavData deserialization** — unchanged; this is the accept/drop gate, not the read.
- **Server-side send logic** — unless step 1 finds the bug is a *missing epoch reset on `SendSubscribe`* (Option
  B), in which case that send-side fix is part of the resolution; otherwise server side is untouched.

## Acceptance criteria

- A determination on record of whether a `kSubscribing` slot legitimately has a server-assigned epoch when
  `ServerCoordStaticData` runs (it should not, per the doc) — i.e. confirming the epoch check there is testing a
  non-meaningful value.
- `ServerCoordStaticData`'s `kSubscribing` handling is made consistent with `ClassifyFullState` /
  `ClassifyCoordUpdate` and the `Client/CLAUDE.md` rule (Option A), **or** the doc gains a documented static-data
  exception and the epoch is correctly seeded (Option B).
- No false drops of once-per-subscription static data (NavData) for `kSubscribing` slots; the subscription
  lifecycle (`kUnsubscribed → kSubscribing → kWaitingFullState → kActive`) still completes correctly under
  either packet ordering.

## Critical files

- `Engine/Source/Network/Client/ClientReceive.cpp` — `Client::ServerCoordStaticData` (`:251`, the
  `kWaitingFullState || kSubscribing` epoch-check condition); reference siblings `ClassifyFullState` (`:68`),
  `ClassifyCoordUpdate` (`:98`), `ServerSubscribeAccept` (the epoch-set site).
- `Engine/Source/Network/Client/CLAUDE.md` — the "Epoch check applies only where the slot has a server-assigned
  epoch" invariant (doc target for Option B; the rule to match for Option A).
- Read-only context: the `SendSubscribe` path (does it seed/reset the slot epoch?), `Engine/Source/Network/
  CLAUDE.md` (slot ACK / epoch model), `Documents/Architecture/Network.md` (subscription lifecycle / epoch guard).

## Notes

- Client-only receive path; **client-side subscription state only — no shared-CRC / determinism exposure** (the
  epoch guard governs which packets are accepted, not simulated state). Risk is a subscription-lifecycle / dropped-
  NavData correctness bug, not desync.
- **Investigation-first** (Diagnosis Discipline) — confirm the `kSubscribing` epoch is non-meaningful at the read
  point before removing it from the condition; the fix is small (one boolean condition) but the *reason* must be
  established so Option A vs B is chosen correctly.
- Sibling of the other audit-surfaced network gate-consistency plans (`ServerUnsubscribeHandshakeGate.md`) — same
  "one handler diverges from its siblings' gate" pattern, here on the client epoch check.
- One grill decision staged: align-to-siblings (A) vs documented-stricter-rule + epoch-seed fix (B), resolved by
  the slot-lifecycle trace in step 1.

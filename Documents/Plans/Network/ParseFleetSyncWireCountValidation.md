# ParseFleetSync: Validate Wire-Supplied Counts Against Payload Size

## Context

`game::ParseFleetSync` (`Projects/BrokenEngineSandbox/Source/Network/PlayerEvents.cpp:64-98`) decodes a
`kServerFleetSync` payload that arrives from the network — a **trust boundary**. Unlike its sibling
`ParsePlayerEvents` in the same file (which gates each branch on `rPayload.size() < 16/17` before reading), this
parser trusts the wire-supplied counts with **no size validation**:

```cpp
int64_t iFleetCount = engine::ReadInt64(pCursor);
rOutFleets.resize(static_cast<size_t>(iFleetCount));
for (int64_t i = 0; i < iFleetCount; ++i)
{
    rOutFleets.at(i).guid.uiHigh = engine::ReadUint64(pCursor);   // advances pCursor unchecked
    ...
    int64_t iMemberCount = engine::ReadInt64(pCursor);
    rOutFleets.at(i).members.resize(static_cast<size_t>(iMemberCount));
    for (int64_t j = 0; j < iMemberCount; ++j) { ...ReadInt64/ReadUint8... }
}
```

`pCursor` (`= rPayload.data()`) is advanced by each `engine::Read*` with no bound against `rPayload.data() +
rPayload.size()`. A truncated or malformed `kServerFleetSync` packet (short payload, or an `iFleetCount` /
`iMemberCount` larger than the bytes actually present) reads **past the end of the payload buffer** (heap
overread, UB) — and a hostile/garbage `iFleetCount` also drives an unbounded `rOutFleets.resize` (the
`Read*` helpers do not bound `iFleetCount` to anything). This violates the root CLAUDE.md trust-boundary
directive ("Do validate anything opaque to the current code unit: network input ...").

The `Network/CLAUDE.md` hub already documents this asymmetry as a known gap ("**Payload validation
asymmetry**: ... fleet-sync parsing trusts wire-supplied counts with no size validation") — this plan closes
it.

Severity: the server is the only sender today, so in normal play the payload is well-formed; the overread is
reachable only via a truncated/corrupt packet or a malicious peer. But it is a genuine unchecked network read,
and the fix is mechanical.

## Design

Bound every read against the remaining payload bytes before consuming them, and reject (do not partially apply)
a malformed packet. Concrete shape:

- Track the payload end (`const uint8_t* pEnd = rPayload.data() + rPayload.size();`). Before each fixed-width
  read or block of reads, check that enough bytes remain (`pCursor + N <= pEnd`); on a shortfall, **abandon
  this packet** — skip it (leave `rOutFleets` in a consistent state) and continue the outer loop, mirroring
  `ParsePlayerEvents`'s `continue` on a short payload.
- Validate `iFleetCount` and each `iMemberCount` are non-negative **and** that the declared element bytes fit
  in the remaining payload before any `resize`, so a garbage count cannot drive a huge allocation or an
  overread. The per-fleet fixed size is `2×uint64 (guid) + int64 (memberCount) + int64 (flagshipIndex) +
  float (navDelay) + iMemberCount × (int64 + uint8)`; compute it from the counts and compare to bytes
  remaining.
- Decide the failure policy in the grill (one decision): **(A)** skip the malformed sync and keep the previous
  `rOutFleets` (last *valid* sync wins — but note the hub currently documents "last sync wins" via
  `erase`; a malformed packet is still `erase`d so it does not re-process); or **(B)** clear `rOutFleets` on a
  malformed sync. Recommendation: **(A)** — a corrupt sync should not blow away a good fleet list; just drop it.
  Either way the consumed entry is `erase`d as today so it is not re-parsed.

Prefer a small bounds-checking read wrapper local to this function (or reuse an existing
`engine::NetworkCursor` bounded-read facility if one exists — check `Engine/Source/Network/NetworkCursor.h`,
which the file already includes) over hand-repeating `pCursor + N <= pEnd` at each site, to keep it DRY and
hard to get wrong. If `NetworkCursor` already offers bounded reads, route the fleet parse through it; that is
the cleanest landing.

## Out of scope

- `ParsePlayerEvents` (same file) — already validates payload size per branch; unchanged.
- The `StatusChange` batch codec (`NetworkSerialization.cpp`) — it has its own post-read truncation detection
  (documented in `Network/CLAUDE.md`); not this parser.
- The `erase`-consumed-entries / "last sync wins" semantics — preserved; this plan only adds bounds checks
  inside the parse, not the packet-draining behavior.
- Any change to the `kServerFleetSync` wire format or to the server's `SendFleetSync` writer — the writer is
  trusted in-codebase; the fix is read-side validation only. (If the grill decides a length prefix would
  simplify validation, that is a wire-format change and a *separate* plan — note only.)
- Hardening other game-layer network parsers beyond fleet-sync.

## Acceptance criteria

- A truncated or count-inflated `kServerFleetSync` payload no longer reads past `rPayload`'s buffer and no
  longer drives an unbounded `resize` — it is rejected (per the chosen failure policy) and the consumed entry
  is removed so it is not re-processed.
- A well-formed fleet-sync still populates `rOutFleets` identically to today (no behavior change on the valid
  path).
- The `Network/CLAUDE.md` "Payload validation asymmetry" bullet is updated to reflect that fleet-sync now
  validates counts (handled via the standard code-change doc step, not in this plan).

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/PlayerEvents.cpp` — `ParseFleetSync` (`:64-98`); the unchecked
  `engine::Read*` sequence at `:78-93` is the change site. `ParsePlayerEvents` (`:13-62`) is the in-file
  template for the size-check pattern.
- `Engine/Source/Network/NetworkCursor.h` — already included by `PlayerEvents.cpp`; check whether it exposes a
  bounded-read cursor to route the parse through (preferred over hand-rolled bound checks).

## Notes

- Trust-boundary network read — validating it is policy-compliant (not defensive validation between our own
  functions).
- No CRC/determinism/`kiVersion` exposure: fleet sync is client-side selection state, not part of the
  shared-CRC sim path or any serialized save layout. Client-only data flow (the server is the producer).
- One grill decision: malformed-packet failure policy (keep-previous vs clear). Otherwise mechanical.

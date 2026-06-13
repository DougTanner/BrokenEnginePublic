# BroadcastTick Blanket Allocation-Suppression Guard Nullifies Documented Tracker-Armed Intent

## Context

`Projects/BrokenEngineSandbox/Source/Network/Server/CLAUDE.md` documents an allocation-tracking invariant:

> **Allocation suppression**: public manager entry points that grow SOA buffers or member containers wrap their
> body in `ScopedSuppressAllocationTracking`. Entry points whose per-tick scratch was migrated to the workbuffer
> intentionally **omit** the guard so the tracker stays armed for them (`BroadcastStatusChanges` is the canonical
> example).

The intent: `ServerBroadcaster::BroadcastStatusChanges` deliberately has **no** `ScopedSuppressAllocationTracking`
(verified — `ServerBroadcaster.cpp`, the function body opens with no guard), so a stray per-tick heap allocation
inside it would trip the tracker and surface a regression.

But its **caller** defeats this. `ServerSession::BroadcastTick` (`ServerSession.cpp`) opens a **function-scope**
guard at the top:

```cpp
void ServerSession::BroadcastTick(int64_t iTick)
{
    // Heap: SendFullState, SendAssignPlayer, and BroadcastUpdate allocate for serialization and compression
    ScopedSuppressAllocationTracking suppress;
    ...
    mpBroadcaster->BroadcastStatusChanges(iTick);   // <- runs entirely inside the caller's guard
    ...
}
```

`ScopedSuppressAllocationTracking` is a **thread-local counter** (`Common/AllocationTracking.h`:
`++giAllocationTrackingSuppressed` / `--`), read by the engine allocator's main-loop tracking
(`Engine/Source/Memory/GlobalAllocator.cpp`). Because the caller's guard is still in scope (counter > 0) for the
entire duration of the `BroadcastStatusChanges` call, the tracker is **suppressed** there — exactly the opposite
of the documented "stays armed" intent. The invariant is silently nullified: a per-tick heap allocation
introduced into `BroadcastStatusChanges` would *not* trip the tracker today.

This plan resolves the contradiction: either **narrow the caller's guard** so `BroadcastStatusChanges` runs with
the counter at zero (restoring the documented armed behavior), or **fix the doc** to admit that the caller's
blanket guard covers it (and drop `BroadcastStatusChanges` as the "canonical armed example").

## Design

Pick one (grill decision):

- **Option A — narrow the caller's guard (restore the invariant).** Replace `BroadcastTick`'s function-scope
  guard with **per-call-site guards** around only the entry points that legitimately allocate
  (`HandleResyncRequests`, `FinalizeNewClients`, the `Send*`/`BroadcastUpdate` paths the `// Heap:` comment
  names), leaving the `mpBroadcaster->BroadcastStatusChanges(iTick)` call **outside** any guard so the tracker is
  armed across it as documented. Verify each narrowed scope still covers its real allocations (the
  `// Heap:` comment enumerates them). This restores the documented "armed" intent and keeps the doc accurate.
  *Caveat:* if any of `BroadcastTick`'s other callees (`SubscriptionUpdates`, `engine::gpServer->Flush()`)
  already carry their own guards, the narrowing is even simpler — confirm which callees self-guard so the
  narrowed caller-guard doesn't double-wrap or leave a gap.
- **Option B — fix the doc.** If the caller's blanket suppression is intentional (BroadcastTick is a coarse
  "this whole broadcast phase allocates" boundary), update the Server `CLAUDE.md` invariant to stop claiming
  `BroadcastStatusChanges` is armed — it isn't, because its sole caller suppresses. Remove/replace the
  "canonical armed example" and state that the tracker is armed only where *no* enclosing caller holds the guard.

**Recommendation: A** — the doc's stated design (workbuffer-migrated entry points stay armed to catch
regressions) is a real safety property worth preserving; a function-scope guard at the caller is the kind of
coarse wrap that silently erodes it. But confirm with the user, since the `// Heap:` comment suggests the author
treated `BroadcastTick` as a single allocating phase on purpose.

Verify the root-cause claim before editing (Diagnosis Discipline): the suppression is a thread-local *counter*
(not a boolean), so nesting accumulates and the caller's `+1` keeps it > 0 through the unguarded callee — confirm
no intervening scope decrements it back to zero before `BroadcastStatusChanges`.

## Out of scope

- **The allocation-tracking mechanism itself** (`AllocationTracking.h` / `GlobalAllocator.cpp`) — unchanged; this
  is about *where* the guard is scoped, not how it works.
- **Other `ScopedSuppressAllocationTracking` call sites** in `ServerSession` / `ServerBroadcaster`
  (`PreTickNetwork`, `ComputeActiveSet`, `BuildFrameInputs`, the resync/load paths) — only `BroadcastTick`'s
  blanket guard over `BroadcastStatusChanges` is in scope. (If Option A's narrowing reveals a *different* entry
  point with the same caller-suppression problem, note it for a follow-up — don't expand this plan.)
- **Adding or removing actual allocations** in `BroadcastStatusChanges` — the goal is to make the tracker armed
  there *as documented*, not to change what it allocates.
- **Determinism / wire format** — none touched; suppression is a debug-tracking concern only (server-only build).

## Acceptance criteria

- Option A: `BroadcastStatusChanges` executes with the suppression counter at zero (tracker armed), so a heap
  allocation introduced there would `DEBUG_BREAK` as the doc intends; `BroadcastTick`'s real allocating callees
  are each still covered by a narrowed guard; server builds and runs without spurious allocation breaks.
- Option B: the Server `CLAUDE.md` invariant no longer claims `BroadcastStatusChanges` is armed; the doc matches
  the actual (caller-suppressed) behavior.
- No CRC / replay / wire-format change either way.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — `ServerSession::BroadcastTick`
  (the function-scope `ScopedSuppressAllocationTracking` + the `mpBroadcaster->BroadcastStatusChanges(iTick)`
  call).
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerBroadcaster.cpp` — `BroadcastStatusChanges` (the
  deliberately-unguarded callee).
- `Projects/BrokenEngineSandbox/Source/Network/Server/CLAUDE.md` — the "Allocation suppression" invariant
  (doc target for Option B; the claim to keep accurate under Option A).
- Read-only reference: `Common/AllocationTracking.h` (the thread-local counter), `Engine/Source/Memory/
  GlobalAllocator.cpp` (the allocator hook that reads it).

## Notes

- **Server-only build** (`BT_SERVER`); no client/CRC/determinism/network-wire exposure. Risk is low — narrowing a
  debug-tracking guard scope (Option A) or a doc edit (Option B).
- One grill decision staged: narrow-the-guard (A, recommended) vs fix-the-doc (B). Confirm whether `BroadcastTick`
  was intentionally treated as one allocating phase (the single `// Heap:` comment hints at that author intent).
- If Option A, the narrowed guards must still cover the real allocators the existing `// Heap:` comment names —
  don't leave a true main-loop allocation unguarded (that would trip the tracker for real).

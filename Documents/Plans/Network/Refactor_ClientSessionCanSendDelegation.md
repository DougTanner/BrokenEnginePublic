# Delegate `game::ClientSession::CanSend` to `engine::Client::CanSend`

Source: follow-up to the just-landed `Refactor_GuardConsolidation.md`, which added `bool engine::Client::CanSend() const` (returning `mbConnected && mpServerPeer != nullptr`) at `Engine/Source/Network/Client/Client.h:104`. The game-side helper at `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.h:53` now duplicates that same predicate by inlining its two halves.

## Context

`game::ClientSession::CanSend` currently reads:

```cpp
bool CanSend() const { return mpClientNetwork != nullptr && mpClientNetwork->IsConnected() && mpClientNetwork->GetServerPeer() != nullptr; }
```

The trailing two clauses (`IsConnected()` AND `GetServerPeer() != nullptr`) are exactly what `engine::Client::CanSend()` already returns. With the engine helper in place the game-side helper should keep only the null-pointer guard on `mpClientNetwork` and delegate the rest:

```cpp
bool CanSend() const { return mpClientNetwork != nullptr && mpClientNetwork->CanSend(); }
```

Behavior is identical (same short-circuit order; same boolean result). The refactor's value is removing the duplicated 2-clause predicate so the engine-side definition is the single source of truth — if `engine::Client::CanSend` ever grows a third condition (e.g., a handshake-complete bit), the game session picks it up automatically.

The 6 call sites in `ClientSession.cpp` (lines 389, 414, 437, 461, 485, 510) are unchanged — they call `CanSend()` on `this`, which now delegates internally.

## Out of scope

- Any other `mpClientNetwork->IsConnected()` callers in the game tree — they may have their own reasons for naming the connected predicate explicitly (different gating semantics, or paired with a non-`GetServerPeer` check). Audit them in a separate plan if duplication appears elsewhere.
- Any change to `engine::Client::CanSend()`'s implementation (still `mbConnected && mpServerPeer != nullptr`).
- Restructuring `ClientSession` (e.g., moving `CanSend` out of the header, extracting other guard helpers, renaming `mpClientNetwork`).
- Migrating `ClientSend.cpp`'s 14 engine-side `if (!CanSend())` sites — they already call the engine `Client::CanSend()` and are the intended pattern.

## Acceptance criteria

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.h:53` body reads `return mpClientNetwork != nullptr && mpClientNetwork->CanSend();` — the game-side helper no longer mentions `IsConnected` or `GetServerPeer`.
- Post-change, the only place in the codebase that mentions `IsConnected()` AND `GetServerPeer()` in the same expression is `engine::Client::CanSend()` itself (verified by `grep -nE 'IsConnected\(\).*GetServerPeer\(\)|GetServerPeer\(\).*IsConnected\(\)'`).
- The 6 `ClientSession.cpp` call sites at lines 389, 414, 437, 461, 485, 510 are unmodified textually; their semantics are preserved through the delegation.
- `BrokenEngineSandbox` client config builds clean. Server config also builds clean (the helper is `BT_CLIENT`-gated through `ClientSession`, but the build sweep catches any stray includes).
- A connect/disconnect playtest confirms send-gating behavior is unchanged: no spurious sends during the `mbConnected = true` / `mpServerPeer == nullptr` window, no sends after disconnect.

## Design

Single-line edit. Keep the `mpClientNetwork != nullptr` short-circuit guard so a destroyed-network state doesn't dereference; replace the two trailing clauses with a single call into the engine helper.

```cpp
// Before
bool CanSend() const { return mpClientNetwork != nullptr && mpClientNetwork->IsConnected() && mpClientNetwork->GetServerPeer() != nullptr; }

// After
bool CanSend() const { return mpClientNetwork != nullptr && mpClientNetwork->CanSend(); }
```

No other files change. No header churn, no new includes (the existing include of `Engine/Source/Network/Client/Client.h` already exposes `engine::Client::CanSend`).

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.h` — the `CanSend` member of `game::ClientSession` (currently line 53; anchor on the symbol name, not the line number).

## Notes

- Follow-up to `Refactor_GuardConsolidation.md` (already landed) — that plan introduced `engine::Client::CanSend()`. This plan finishes the deduplication on the game side.
- Pure delegation, no behavior change. Score 0 (Effort 1, Impact 1, Risks 0) — the lowest priority bucket; queue it whenever a contributor is touching `ClientSession.h` for another reason.
- If a future audit finds analogous 3-clause `nullptr && IsConnected() && GetServerPeer()` patterns elsewhere (e.g., a future game-tree manager that holds its own `engine::Client*`), the same delegation applies; not in scope here.

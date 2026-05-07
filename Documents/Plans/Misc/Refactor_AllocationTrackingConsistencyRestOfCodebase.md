# Refactor: ScopedSuppressAllocationTracking Consistency (Rest of Codebase)

## Context

`/next-plan` recently landed `Refactor: ScopedSuppressAllocationTracking Consistency` on the Network subtree. That pass scoped only `Engine/Source/Network/` and `Projects/BrokenEngineSandbox/Source/Network/`. A repo-wide grep finds ~17 remaining call sites outside the Network subtree that still use the long Hungarian local-variable name `scopedSuppressAllocationTracking`.

Two style/convention violations apply:

- **Hungarian rule 3** (`Documents/C++StyleGuide.txt`): locals do not carry a type-prefix like `sc`/`scoped`. The local should be the bare camel-case name `suppress`.
- **Allocation-discipline convention** (`Engine/Source/CLAUDE.md` and `Engine/Source/Frame/Collections/CLAUDE.md`): every `ScopedSuppressAllocationTracking` guard should be preceded (or annotated inline) by a `// Heap: <reason>` comment naming the operation that allocates. Most surveyed sites already comply; a small number do not.

This plan is the mechanical sweep that completes the rename + comment audit across the rest of the engine and the game-side `Game.cpp`. It is intentionally narrow: rename the local, add the missing `// Heap:` comments, and flag any guard whose scope no longer contains a real heap operation for executor judgement.

## Design

For every site below, the executor performs one or more of:

1. **Rename** the local variable from `scopedSuppressAllocationTracking` to `suppress`. (Always.)
2. **Add `// Heap: <reason>` comment** on the line directly above the guard, naming the specific container/operation that allocates. Apply only to sites flagged `rename+comment-add`.
3. **Evaluate** whether the guard is still needed; if every operation in scope is provably non-allocating (or the scope is provably init-time only), delete the guard entirely. Apply only to sites flagged `evaluate`.

No semantic change is intended at any rename-only site. Comment-add sites grow by one line of comment. Evaluate sites may shrink by one or more lines if the guard is removed; if retained, they become rename-only.

### Per-site categorization

| # | File:line | Enclosing symbol | Action |
|---|-----------|------------------|--------|
| 1 | `Engine/Source/Main.cpp:223` | `wWinMain` boot-time render-interpolate populate (single-origin frame block) | rename-only — `// Heap: operator[] may insert default element` already present line 222 |
| 2 | `Engine/Source/Main.cpp:316` | `wWinMain` server-build per-tick `WM_PAINT`-equivalent (`InvalidateRect` + `ServerUpdateDisplayStats`) inside the `BT_SERVER` else-branch of the main loop | rename-only — `// Heap: Win32 InvalidateRect may trigger internal GDI allocations` already present line 315 |
| 3 | `Engine/Source/Main.cpp:499` | `WindowProc` `WM_PAINT` server display branch | rename-only — `// Heap: GDI painting creates/destroys kernel objects that may trigger CRT allocations` already present line 498 |
| 4 | `Engine/Source/GameBase.cpp:355` | `GameBase::PrepareRenderInterpolates` (post-`mfRenderTime` clamp block) | rename-only — `// Heap: std::erase_if may rehash, operator[] may insert` present inside scope line 357 |
| 5 | `Engine/Source/GameBase.cpp:436` | `GameBase::PrepareActiveSet` replay branch (`#if defined(BT_SERVER)`) | rename-only — `// Heap: vector clear/push_back, unordered_map insertion + make_unique<Frame>` present line 435 |
| 6 | `Engine/Source/GameBase.cpp:477` | `GameBase::PrepareActiveSet` replay tail (`make_unique<game::Frame>` for the replay target coord) | rename-only — `// Heap: make_unique<Frame> for replay target coordinate` present line 476 |
| 7 | `Engine/Source/Profile/ProfileManagerBase.cpp:104` | `ProfileManagerBase::CpuStart` (cross-thread state map populate; `try_emplace` + `resize`) | **rename + comment-add** — guard exists with no `// Heap:` annotation; add comment naming `try_emplace` map insertion and `vector::resize` to `kCpuTimerCount` |
| 8 | `Engine/Source/Profile/ProfileManagerBase.cpp:150` | `ProfileManagerBase::CpuStop` same-thread branch (vector resize to `GetCpuTimerCount()` on first hit) | **rename + comment-add** — guard exists with no `// Heap:` annotation; add comment naming `vector::resize` to `kCpuTimerCount` |
| 9 | `Engine/Source/Server/ServerDisplay.cpp:317` | `PaintProfilePanel` (`sProfileText` `std::string` reset for clipboard cache) | rename-only — `// Heap: std::string operations for clipboard cache` already present line 316 |
| 10 | `Engine/Source/Frame/Collections/WindTrails/WindTrailsRender.cpp:71` | `WindTrails::Render` per-trail render-state map walk (file-scope `sRenderState`) | rename-only — `// Heap: unordered_map insertions/lookups for per-trail previous positions` already present line 70 |
| 11 | `Engine/Source/Frame/Collections/Collection.h:726` | `EraseStaleRenderState` template helper | rename-only — `// Heap: unordered_map erase for stale render state entries` already present line 725 |
| 12 | `Projects/BrokenEngineSandbox/Source/Game.cpp:89` | `Game::AddClientPlayer` (push_back into `mClientPlayerIds` and `mClientPlayerCoords`) | **rename + comment-add** — guard exists with no `// Heap:` annotation; add comment naming `mClientPlayerIds` / `mClientPlayerCoords` `push_back`. Called from `ClientSession.cpp:93` per-player join — main-loop adjacent, guard is justified |
| 13 | `Projects/BrokenEngineSandbox/Source/Game.cpp:234` | `Game::SyncFleets` (rebuilds `mClientFleets` from received list; `LOG(kVerbose)` immediately inside) | **rename + comment-add** — guard exists with no `// Heap:` annotation; add comment naming the `mClientFleets` rebuild + `LOG` arg formatting allocations |
| 14 | `Projects/BrokenEngineSandbox/Source/Game.cpp:397` | `Game::ComputeActiveSet` client branch (`mActiveCoords.clear()/push_back`) | rename-only — `// Heap: mActiveCoords vector clear/push_back may allocate. Persists as Game member across frame updates` already present line 396 |
| 15 | `Projects/BrokenEngineSandbox/Source/Game.cpp:1060` | `Game::SaveGraphicsSettings` (file I/O via `engine::WriteVersionedFile`) | rename-only — `// Heap: file I/O allocates` already present line 1059 |
| 16 | `Projects/BrokenEngineSandbox/Source/Game.cpp:1223` | `Game::SaveClientState` (file I/O via `engine::WriteVersionedFile`) | **rename + comment-add** — guard exists with no `// Heap:` annotation; add comment naming `engine::WriteVersionedFile` file I/O |
| 17 | `Projects/BrokenEngineSandbox/Source/Game.cpp:1236` | `Game::LoadClientState` (file I/O via `engine::ReadVersionedFile`) | **rename + comment-add** — guard exists with no `// Heap:` annotation; add comment naming `engine::ReadVersionedFile` file I/O |

Summary: **12 rename-only**, **5 rename + comment-add**, **0 evaluate-and-delete**. None of the 17 sites looked obviously removable on inspection — every guard wraps either a real container mutation, a `make_unique`, file I/O, a `LOG` arg path, or a Win32/GDI call known to allocate internally.

## Critical files

- `Engine/Source/Main.cpp` — 3 sites (boot, server tick, WndProc)
- `Engine/Source/GameBase.cpp` — 3 sites (`PrepareRenderInterpolates`, `PrepareActiveSet` replay paths)
- `Engine/Source/Profile/ProfileManagerBase.cpp` — 2 sites (`CpuStart`, `CpuStop`); both need comment-add
- `Engine/Source/Server/ServerDisplay.cpp` — 1 site (`PaintProfilePanel`)
- `Engine/Source/Frame/Collections/WindTrails/WindTrailsRender.cpp` — 1 site (`WindTrails::Render`)
- `Engine/Source/Frame/Collections/Collection.h` — 1 site (`EraseStaleRenderState` template)
- `Projects/BrokenEngineSandbox/Source/Game.cpp` — 6 sites (`AddClientPlayer`, `SyncFleets`, `ComputeActiveSet`, `SaveGraphicsSettings`, `SaveClientState`, `LoadClientState`); 4 need comment-add

## Notes

- The Network-subtree pass landed the same rename and gave the convention its current shape — this plan is the symmetric cleanup. Nothing here is novel.
- `Common.h` exposes `ScopedSuppressAllocationTracking`; the type name itself does not change. Only the local variable name and accompanying `// Heap:` comments do.
- Hungarian rule 3 in `Documents/C++StyleGuide.txt` is the load-bearing reference for the rename direction.
- `Engine/Source/CLAUDE.md` and `Engine/Source/Frame/Collections/CLAUDE.md` already document the `// Heap:` comment expectation; no doc updates are required by this plan.
- Comment text should name the *specific* container or call — "operator[] may insert default element", "make_unique<Frame> for replay target", "engine::WriteVersionedFile file I/O" — not generic "allocation" hand-waving. The categorization table above gives the recommended phrasing for each comment-add site.

## Out of scope

- Any change to the `ScopedSuppressAllocationTracking` RAII type itself (in `Common/`).
- Any audit of guards inside `Engine/Source/Network/` or `Projects/BrokenEngineSandbox/Source/Network/` — already landed.
- Removing or restructuring guards whose scope is genuinely justified — this is a comment + rename pass, not a guard-removal pass. Sites flagged `rename + comment-add` keep their guards; the guard-removal-evaluate column is empty.
- Refactoring the surrounding functions (`PrepareActiveSet`, `SyncFleets`, `CpuStart`, etc.) for any reason beyond the local rename and the one-line comment add.
- Eliminating heap allocations themselves at any of these sites (e.g., migrating to `gpThreadLocal->mWorkbuffer`). That work belongs to dedicated per-site plans where justified.

## Acceptance criteria

- Repo-wide grep for `scopedSuppressAllocationTracking` returns zero hits.
- Every `ScopedSuppressAllocationTracking` instance in the rest-of-codebase site list above has a `// Heap: ...` comment within the immediately preceding 2 lines or inline above the heap operation it covers.
- Both `BrokenEngineSandbox` client and server vcxproj configurations build clean (no new warnings, no LNK errors that aren't pre-existing run-locked-file issues).
- No semantic diff at runtime — the only behavioural changes are local-variable identifier and added comment text.

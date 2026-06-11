# Refactor: Frame Style Mechanics Sweep

## Context

Source: /external-refactor-clean on `Engine/Source/Frame` (non-recursive). Grouped mechanical style/idiom fixes, each verified against `Documents/C++StyleGuide.txt` or root-CLAUDE.md patterns. All compile-checked, zero behavior change.

## Design

### Engine/Source/Frame/NavQuery.cpp
- Replace the only two raw m128 accesses in the directory with the function form: `XMVector3LengthSq(...).m128_f32[0]` → `XMVectorGetX(...)` at lines 616 and 653 (the file already does it right at line 685) [~5m]

### Engine/Source/Frame/Collision.cpp
- `sTestedBGeneration[j]` → `sTestedBGeneration.at(static_cast<size_t>(j))` at lines 437, 441 (style rule 16; line 435 in the same loop already uses `.at()`) [~5m]
- Bare `memset` → `std::memset` at lines 395, 417 (rule 41; `Alignments.cpp:64` shows the house form) [~2m]
- Positional `{-1, 0}` → designated `{.iOffset = -1, .iCount = 0}` at line 375 (rule 42) [~2m]

### Engine/Source/Frame/FrameBase.cpp
- Strip `[[maybe_unused]]` from the 12 signatures whose parameters are unconditionally used (lines 165–266, e.g. `AllocateAndCopy:165`, `PreCollision:225`); keep it only on `rFrameInput` in `FramePostRenderBase::Update` (line 199), the one genuinely unused parameter (rule 39) [~10m]
- Rename the lambda parameter pack `cols` → unabbreviated (`collections`) at lines 14, 40, 52, 64, 79, 116, 133, 147 (rule 56) [~5m]

### Engine/Source/Frame/FrameUtils.h
- Normalize template parameter names at lines 47, 50, 56, 139 (`Tuple` ×2, `Ts`, `TTupleCurrent`/`TTuplePrevious`, `Is`) to the rule-19 all-uppercase convention already used at lines 42, 60 [~5m]

### Engine/Source/Frame/GridCoord.h
- Drop redundant `inline` on in-class member definitions at lines 24, 29, 35 [~2m]

### Engine/Source/Frame/Alignments.cpp
- `static auto AlignmentKeyLess` → `static constexpr auto` at line 9 [~2m]

### Engine/Source/Frame/IslandChainPlacement.h
- `IslandChainPlacement` (lines 20–25) is a stateless class with a single static member function — replace with a free function (e.g. `GenerateIslandChain(GridCoord, std::vector<IslandPlacement>&)`); update the two call sites (`Game.cpp:372, 500`) [~10m]

## Critical files
- `Engine/Source/Frame/NavQuery.cpp`, `Collision.cpp`, `FrameBase.cpp`, `FrameUtils.h`, `GridCoord.h`, `Alignments.cpp`, `IslandChainPlacement.h`, `IslandChainPlacement.cpp`
- `Projects/BrokenEngineSandbox/Source/Game.cpp` (two call sites)

## Out of scope
- The `ForEach*` fold-helper collapse (investigated, rejected: trades 40 lines of trivially readable repetition for `[&]<typename T>()` template-lambda machinery — fails KISS)
- `ApplyMovement`'s 7 parameters (XM_CALLCONV register passing keeps the XMVECTORs loose; bundling the four floats judged marginal — dropped)
- Member initializers on `PendingCollisionResult`/`ZoneRange` (folded into `Architecture_CollisionHeaderSurface.md`, which moves those types)
- Anything behavioral

## Notes
- No determinism/CRC exposure. Shares `FrameBase.cpp` with `Architecture_LogDifferencesServerCollections.md`/`Architecture_CrcMixingStrength.md`, `Collision.cpp` with the two Collision plans, and `NavQuery.cpp` with the nav plans — fold into whichever session touches the same file, or land last with refreshed lines.

## Verification Notes (2026-06-10)
- Every item re-verified against source and `Documents/C++StyleGuide.txt`:
  - NavQuery `.m128_f32[0]` at `:616` and `:653` confirmed; `:685` uses `XMVectorGetX` (the in-file house form). No numbered rule covers raw `m128` access — justified by codebase consistency + DirectXMath accessor convention; behavior-identical (lane 0 read either way).
  - Collision.cpp: raw `sTestedBGeneration[j]` at `:437/:441` vs `.at()` at `:435` — rule 16 explicitly extends to vectors ("Access std::vectors with .at()", StyleGuide:95). Bare `memset` at `:395/:417` (rule 41; `Alignments.cpp:64` `std::memcpy` precedent confirmed). Positional `{-1, 0}` at `:375` (rule 42).
  - FrameBase.cpp: 13 `[[maybe_unused]]` signatures total (`:165/:172/:192/:199/:225/:230/:235/:240/:245/:250/:256/:261/:266`); checked each parameter — all are used unconditionally (passed to `ForEach*` or read directly; the Interpolate `Update`'s `rPreviousFrame` is used at `:189` outside the `BT_CLIENT` span) **except** `rFrameInput` in `FramePostRenderBase::Update` (`:199`), which is genuinely unused in the body (`:200-223`). Plan's "strip 12, keep only rFrameInput" is exactly right. The `cols` pack at `:14/:40/:52/:64/:79/:116/:133/:147` all confirmed (rule 56).
  - FrameUtils.h: cite corrected to add line 56 (`template<typename Tuple> using TupleToTypeList_t` — same mixed-case name as line 47). Rule 19 ("template parameters should be in all upper case") confirmed.
  - GridCoord.h redundant `inline` at `:24/:29/:35` confirmed (in-class definitions, implicitly inline).
  - `Alignments.cpp:9` `static auto` lambda confirmed; `constexpr` is valid for the captureless comparator.
  - `IslandChainPlacement` (h:20-25) is a stateless class with the single static `Generate`; both call sites confirmed at `Game.cpp:372` and `:500`. Free-function conversion matches the directory's existing pattern (`BuildNavContour`/`BuildCellNavData` are free functions).
- Out-of-scope rejections spot-checked and left intact (the `ForEach*` fold collapse and `ApplyMovement` bundling rationales are consistent with KISS and `XM_CALLCONV` register passing; the member-initializer item is correctly owned by `Architecture_CollisionHeaderSurface.md`).

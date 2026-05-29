# Smoothed: guard integer-only drift, fix Max() seeding, expose capacity constant

## Context

`common::Smoothed<VALUE_TYPE, COUNT>` (`Common/Smoothed.h`) is a fixed-size rolling buffer with a "slow-drift near target, snap on large gap" `Update()` step (per `Common/CLAUDE.md`). Two instantiations exist in the codebase:

- `Smoothed<int64_t>` (default `COUNT = 128`) — network/profile telemetry; `Projects/BrokenEngineSandbox/Source/Profile/NetworkGraphs.cpp` and `ProfileManager`.
- `Smoothed<float, 256>` — `Engine/Source/Frame/TimeStep.h:70` (`mAverageDelta`), read only via `GetAverageDelta() -> mAverageDelta.Average()`.

Three real defects surfaced by analysis, all verified against source (no phantom findings; every cited line exists):

1. `Update()`'s drift step is integer-only and silently wrong for any non-integral `VALUE_TYPE` — **latent** because the only float instance never calls `Update()` (confirmed: no `.Update()` call anywhere touches `mAverageDelta`).
2. `Max()` seeds its accumulator with `{}` (zero), so an all-negative dataset returns `0` instead of the true maximum — **latent** (current `Max()` callers feed non-negative microsecond data).
3. `NetworkGraphs.cpp` hard-codes `kiSmoothedCapacity = 128`, silently duplicating the `Smoothed<int64_t>` default `COUNT`. If `COUNT` ever changes for that type, the graph's index wrap-around (`mpValues[iActual]`) reads the wrong slots with no compile error. `Profile/CLAUDE.md` already flags this: "the hard-coded capacity must track `common::Smoothed<T>`."

## Design

- **Guard `Update()` to integral `VALUE_TYPE`** — `the Update() drift step in Smoothed<VALUE_TYPE,COUNT>` (`Common/Smoothed.h:119-140`). The `diff = -1` / `diff = 1` unit-step clamp and the `(-COUNT, COUNT)` snap band only make sense for integral counts; for `float` they crawl in `1.0f` steps and never snap on sub-1.0 deltas. Add `static_assert(std::integral<VALUE_TYPE>);` as the first line of `Update()` so any future non-integral caller fails at compile time instead of silently misbehaving. This is the minimal, KISS fix that pins the documented contract without disturbing the working `int64_t` callers or the `float` `Average()`-only path. (Effort 1) Do NOT re-template the whole class to `std::integral` — the `float` instance legitimately uses `operator=`/`Seed`/`Average` and must keep compiling. (Risk 1)

- **Fix `Max()` seeding** — `Max()` in `Smoothed<VALUE_TYPE,COUNT>` (`Common/Smoothed.h:43-68`). Replace the `VALUE_TYPE max = {};` seed (line 50) with the first folded element: seed `max` from `mpValues[iCurrent]` (the most-recent sample, already computed as the loop's starting index) before the fold, or fold starting from the second element. The `miCount == 0` early-out (lines 45-48) already guarantees at least one element exists, so seeding from the first sample is safe. (Effort 1) `Average()`'s `total = {}` is correct (additive identity) and is out of scope. (Risk 1)

- **Expose a public capacity constant and consume it** — add `static constexpr int64_t kCapacity = COUNT;` to `Smoothed` (`Common/Smoothed.h`, beside the public ring fields). Then in `Projects/BrokenEngineSandbox/Source/Profile/NetworkGraphs.cpp`, delete the local `static constexpr int64_t kiSmoothedCapacity = 128;` (line 12) and replace its two uses (lines 21, 22) with `common::Smoothed<int64_t>::kCapacity`. `SmoothedGetter` already reaches into the public `miNext`/`miCount`/`mpValues` members (deliberate, per `Profile/CLAUDE.md`), so reading a public static constant is consistent with the existing access pattern. (Effort 2) (Risk 1)

## Critical files

- `Common/Smoothed.h` — `Update()` static_assert, `Max()` seed fix, new `kCapacity` constant.
- `Projects/BrokenEngineSandbox/Source/Profile/NetworkGraphs.cpp` — consumer; drop the duplicated `128` literal, reference `Smoothed<int64_t>::kCapacity`. `#if defined(BT_CLIENT)`-wrapped, client vcxproj only.
- `Projects/BrokenEngineSandbox/Source/Profile/CLAUDE.md` — update the "hard-coded capacity must track `common::Smoothed<T>`" note to reflect that it now derives from `kCapacity` (no longer a manual literal).

## Out of scope

- Threading / data-race documentation on `Smoothed` / `InTheLastSecond` (no concrete cross-thread misuse demonstrated; speculative).
- `operator=` return type (`void`) and `Average()`'s `return total /= ...` clarity — operator-= return type and compound-assign-vs-divide are code-style-review territory; behavior is correct for both current instantiations.
- `const`-marking `Get()`/`Max()`/`Current()` — `misc-const-correctness` is deferred in `.clang-tidy`; owned by code-style-review.
- Renaming `mpValues` (C array under a `mp` pointer prefix) — cosmetic; would churn the `NetworkGraphs.cpp` reader for no behavior change.
- `InTheLastSecond` overflow ceiling (>1024 `Set`/sec), modulo-invariant comments, `Set` non-negativity, and a `high_resolution_clock::is_steady` static_assert — soft/latent documentation items with no live caller hitting them.
- Re-templating `Smoothed` onto `ExponentialInterpolant`/`ExponentialDecay` for a future float drift path — YAGNI; no caller needs float `Update()` today.

## Acceptance criteria

- `Update()` contains `static_assert(std::integral<VALUE_TYPE>);`; `Smoothed<float, 256>` still compiles (it never instantiates `Update()`); a hypothetical `Smoothed<float>::Update()` instantiation would fail to compile.
- `Max()` on an all-negative sample set returns the true (negative) maximum, not `0`.
- `Smoothed` exposes `static constexpr int64_t kCapacity`; `NetworkGraphs.cpp` no longer contains a literal `128` for the buffer size and references `kCapacity` instead.
- DataPacker + BrokenEngineSandbox (client) build clean.

## Notes

- `static_assert` failures need a diagnosed instantiation; since no current caller instantiates the float `Update()`, the assert is dormant until someone adds a non-integral `Update()` user — exactly the intended trip-wire.
- `std::integral` is already available (C++23, `<concepts>`); confirm it's reachable via the aggregation headers — if not, the include belongs in `Common/ExternalHeaders.h`, not in `Smoothed.h`.
- Effort 2, Impact 3 (two latent correctness fixes + one silent-mismatch maintainability hazard), Risk 1.

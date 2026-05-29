# Random.h / Random.cpp — Correctness, Bounded-Draw, and Determinism Hardening

## Context

`common::RandomEngine` (Xorshift64 seeded via splitmix64) is the engine-wide deterministic RNG: it is CRC-folded into the frame checksum, `LogDifference`-diffed for desync diagnosis, serialized for save/replay, and copied forward each tick. Per `Common/CLAUDE.md`, all gameplay randomness flows through it ("never `std::random`"). Three real defects exist in the draw paths plus a couple of cheap determinism guardrails worth adding:

- The float draw can return a value `>= MAX`, violating the documented half-open `[0, MAX)` contract that index-pick call sites (`Random() * count`) and jitter (`MathUtils.cpp` jitter, `-fMaxJitter + Random<2.0f>() * fMaxJitter`) rely on. It also discards ~40 generator bits to a lossy `uint64 -> float` cast whose result depends on FP rounding mode.
- The bounded integer draw `% (uiMax + 1)` is integer-divide-by-zero UB when `uiMax == UINT32_MAX` (a valid argument), and is modulo-biased while throwing away the strong high bits of the stream.
- The float-generation algorithm is duplicated (template overload in the header, runtime overload in the .cpp), so any fix must be applied twice and can silently drift (DRY).

All draw-path changes alter the produced sequence and therefore every existing recording/CRC. This is a deliberate, replay-breaking change — must be coordinated with whatever replay/save-version mechanism exists and verified against saved replays.

## Design

### 1. Half-open float draw, deduplicated (validated H1 + M2) — [Small]
- Replace the lossy `static_cast<float>(RandomNext(...)) * (MAX / static_cast<float>(UINT64_MAX))` pattern with an explicit high-24-bit construction that is exact (no rounding-mode dependency) and always strictly `< MAX`:
  - `RandomNext(...) >> 40` lands in `[0, 2^24)`; the `int -> float` cast is exact; multiply by `MAX / 2^24f`.
- Factor a single inline helper in `Common/Math/Random.h` (e.g. `inline float RandomUnitFloat(RandomEngine&)` returning `[0, 1)`) so both public overloads share one algorithm:
  - the compile-time template `Random<MAX>(RandomEngine&)` at `Common/Math/Random.h:32-37` becomes `RandomUnitFloat(...) * MAX`.
  - the runtime overload `Random(float fMax, RandomEngine&)` at `Common/Math/Random.cpp:43-46` becomes `RandomUnitFloat(...) * fMax`.
- This collapses M2 (one source of truth) into the H1 fix.

### 2. Bounded integer draw: fix UINT32_MAX UB + remove bias + use strong bits (validated H2 + H3) — [Small]
- `Random(uint32_t uiMax, RandomEngine&)` at `Common/Math/Random.cpp:38-41`. Both findings collapse to one rewrite using Lemire multiply-shift on the full 64-bit draw, computing the range in 64-bit:
  - range `= static_cast<uint64_t>(uiMax) + 1` (64-bit add cannot wrap → fixes the `% 0` UB at `UINT32_MAX`).
  - result `= static_cast<uint32_t>(((RandomNext(...) >> 32) * range) >> 32)` (division-free, uses the high 32 bits, bias bounded to `range / 2^32`).
- One draw per call (preserves the "every `Random` call advances the engine exactly once" contract relied on at the navigation call sites).

### 3. Determinism guardrails — [Quick Win]
- Add `static_assert(sizeof(RandomEngine) == sizeof(uint64_t), "extend operator== / Crc() / serialization when adding RandomEngine state");` beside the struct in `Common/Math/Random.h` (validated L4). Forces any future state addition to revisit the three desync-detection sites.
- Add `static_assert(std::chrono::high_resolution_clock::is_steady, ...)` inside `RandomEngine::TimeSeed()` in `Common/Math/Random.cpp:19-26` (style rule 31; a non-steady clock can run backwards). (validated, trimmed M1)

### 4. Header documentation (doc-only, fold in while editing the header) — [Quick Win]
- One-line comment that `RandomEngine` is **not** thread-safe: each worker/partition needs its own deterministically-seeded instance; sharing across threads is a data race and breaks determinism. (validated, trimmed M5)
- One-line comment that the default state is shared by design (deterministic for replay) and every *independent* stream must be explicitly seeded. (validated, trimmed M3)
- One-line comment on `RandomEngine::TimeSeed()`: NOT deterministic — never seed a simulation engine from this without broadcasting/recording the resulting seed. (validated, trimmed M1)

## Critical files
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Math\Random.h` — float helper + both float overloads, the two `static_assert`s, the doc comments.
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Math\Random.cpp` — bounded-integer rewrite, runtime float overload, `TimeSeed` `static_assert`.

## Out of scope
- **Auditing every `TimeSeed()` call site to confirm seeds are broadcast/recorded.** The original M1 worry that `rFrame.postRender.randomEngine.TimeSeed()` (`Game.cpp`) seeds the CRC-folded simulation engine per-machine is a *correctness/architecture* question for the user, not a mechanical fix. If TimeSeed is genuinely being applied to a simulation-replay engine without broadcasting the seed, that is a separate desync bug needing its own plan — surface it; do not change call-site behavior here.
- **64-bit seed overload (M4).** No current call site demonstrably collides in the 2^32 seed space; adding API surface is YAGNI. Skip unless a real collision is shown.
- **Debug "first draw on still-default state" tripwire (M3).** Speculative; the doc comment covers the contract.
- **Pure style nits (L1 `x` locals, L2 `template <` spacing, L3 hex digit separators, L5 `[[nodiscard]]` on `Crc()`/`operator==`).** Leave to the code-style-review pass.
- **GPU `Engine\Data\Shaders\ShaderRandom.h`.** Intentionally a separate 32-bit width, not part of the CPU determinism contract.
- Do NOT add error handling / parameter validation (project directive).

## Acceptance criteria
- `Random<1.0f>()` and `Random(fMax, ...)` return strictly `< MAX` / `< fMax` for every possible draw (including the top-of-range state); the half-open contract holds without depending on FP rounding mode.
- `Random(UINT32_MAX, engine)` returns a valid value with no divide-by-zero (UB removed); the full inclusive range `[0, uiMax]` is reachable.
- Float and bounded-integer logic each live in exactly one place.
- Both `static_assert`s compile.

## Notes (validation)
- Validated against the actual source: `Random.h` (41 lines) and `Random.cpp` (48 lines) read directly. H1 confirmed at `Random.h:35-36` / `Random.cpp:45`; H2 + H3 confirmed at `Random.cpp:40` (`% (uiMax + 1)` with `static_cast<uint32_t>` before the modulo); M2 duplication confirmed across `Random.h:32-37` vs `Random.cpp:43-46`.
- `common::Random` call sites confirmed present (grep) in `MathUtils.cpp`, `IslandChainPlacement.cpp`, `FleetNavigationController.cpp`, `PlayersNavigation`, `LightingUniforms.cpp`, `TextureCache.cpp`, `SmokeTrailsRender.cpp`, `Game.cpp`, `ServerFleetManager.cpp`, `StaticVoices.cpp`, `FrameBase.h` — matching the report's claimed usage breadth, so the half-open / bounded-draw contracts are genuinely depended upon.
- No `std::` distributions, `rand()`, or `std::mt19937` in the CPU RNG path — only ThirdParty (out of scope). PASS.
- **Replay/CRC risk**: items 1 and 2 change the produced sequence; this is the intended fix but breaks bit-compatibility with existing recordings. Treat as a deliberate versioned change (Risk 3). Items 3 and 4 are byte-neutral.
- Dropped findings and rationale recorded in the agent return (DROPPED section), not duplicated here.

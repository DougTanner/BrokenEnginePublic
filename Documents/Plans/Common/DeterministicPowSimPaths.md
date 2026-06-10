# Deterministic pow on CRC-Fed Sim Paths

## Context

The determinism sweep that added `common::DeterministicSinCos` (`Common/Math/MathUtils.h:219`, wrapping the header-inline minimax `XMScalarSinCos`) and stripped libm `std::cos`/`std::sin` out of the CRC-fed sim paths (`IslandChainPlacement.cpp` `BuildWorldHull`, `IslandTerrain::BuildElevationGrid`, `NavBuild::BuildCellNavData`) deliberately deferred **two remaining transcendental sites that use `pow`, not sin/cos**, to this follow-up. Both feed the shared CRC in both client and server builds, so a cross-toolchain libm `powf` rounding difference is a latent desync source per `Documents/FloatingPointDeterminism.txt` §5 ("No direct calls to sinf/cosf/tanf … avoiding them eliminates this class of non-determinism entirely" — `pow` is the same class).

Unlike `XMScalarSinCos` (a self-contained polynomial of IEEE basic ops, bit-identical under `/fp:strict`), `XMVectorPow` is **not** safe: it routes to per-component libm `powf` (`Windows Kits/10/Include/.../um/DirectXMathVector.inl:4094` — `_XM_NO_INTRINSICS_`/NEON branches call `powf`, the Intel SVML branch calls `_mm_pow_ps`; none is a closed-form polynomial). `std::pow` is libm directly.

The two sites:

- **`PushersInterpolate::ApplyPush`** (`Engine/Source/Frame/Collections/Pushers/PushersUpdate.cpp:173`): `vecIntensity = XMVectorPow(vecIntensity, XMVectorReplicate(rCurrent.pfPowers[i]))` — the `(1 - d²/r²)^power` push falloff. Called from `SpaceshipsNavigation.cpp:106` and `PlayersNavigation.cpp:454`, result flows through `ApplyClampedPush` (`Pushers.h:27`) into `rVecVelocity` — the shared CRC'd, serialized velocity field, in **both builds** (`Pushers` compiles unguarded into client and server). **The exponent is an integer in practice**: `pfPowers[i]` is set from `SyncData::fPower` (`PushersUpdate.cpp:49`), whose only two producers are the compile-time constants `kfSpaceshipPusherPower = 3.0f` (`Spaceships.h:33`) and `kfPlayerPusherPower = 2.0f` (`Players.h:40`). No runtime slider feeds it. So `XMVectorPow` here is always `x^3` or `x^2`.
- **`PlayersPostRender::SpawnDeathExplosions`** (`Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersCombat.cpp:476`): `fAdjustedPercent = (std::pow((1.0f - fPercent) + 1.0f, kfDeathRadialPower) - 1.0f) * kfExplosionsRadius`, the radial offset of each death-explosion spawn. `kfDeathRadialPower = 0.3f` (`PlayersCombat.cpp:53`) is a compile-time constant but an **arbitrary fractional exponent** (no exact integer/half-integer rewrite). The resulting `vecJitteredPosition` becomes the explosion spawn's `vecPosition` (`PlayersCombat.cpp:482`), which is in the shared CRC in both builds (the explosion `Random*` jitter already runs unconditionally on both sides — this is a sim spawn, not visual-only). Base is always `≥ 1.0` here (`(1 - fPercent) + 1` with `fPercent ∈ [0,1]`), so no negative-base NaN concern.

**Value-drift consequence (accept, same class as the sincos change):** swapping libm `pow` for a deterministic approximation changes the numeric result slightly, so already-recorded local replays and any in-flight client/server sessions reset — identical accepted consequence to the `DeterministicSinCos` swap (locally-recorded replays reset; no persisted `.pack`/`.manifest`/save CRC changes since none of these feed asset bakes). No `Frame::kiVersion` / save-layout bump (serialized layout is unchanged; only computed values shift). Document this in the plan's notes and at each call site.

## Design

Two independent sites, two different fixes. Verify the exponent facts above against source at execution before choosing (the integer-exponent finding is the load-bearing simplification — if a future change makes `kf*PusherPower` a slider, fall back to the general helper).

### Site 1 — Pushers integer-exponent falloff (simplest: exact repeated multiply)

Because the exponent is provably `2.0f` or `3.0f` (compile-time constants, no slider), replace `XMVectorPow(vecIntensity, XMVectorReplicate(rCurrent.pfPowers[i]))` with an **exact integer power by repeated multiply** — IEEE multiply is a basic op, bit-identical under `/fp:strict`, no approximation error. `pfPowers` is a per-element `float` SOA field, so the exponent is not a compile-time constant *at the `ApplyPush` site*; resolve via one of:

- **(1a) Round the stored power to an integer and dispatch a small `switch`/loop** (`x*x`, `x*x*x`, general `for` of `XMVectorMultiply`). Keeps `pfPowers` a float field (no SOA/CRC layout change — `pfPowers` stays in `SharedMembers()` and the serialized layout), so save/replay layout is untouched; only the *math* changes. A `common::DeterministicIntPow(XMVECTOR vecBase, int32_t iExp)` helper in `MathUtils.h` (exponentiation-by-squaring over `XMVectorMultiply`, mirrors the `DeterministicSinCos` placement/comment convention) is the DRY home if a second integer-pow site ever appears; otherwise inline the two-case form.
- **(1b)** If a fractional pusher power is ever genuinely needed (not today), route through the Site-2 general `DeterministicPow` helper instead — but do NOT build that generality now (YAGNI); the integer path is exact and cheaper.

Recommended: **(1a)** with a tiny shared `DeterministicIntPow` helper, since two distinct exponent values already exist. Confirm at execution that no `pfPowers` value is non-integral (it is `float`-typed but only ever assigned `2.0f`/`3.0f`); add an `ASSERT` that the rounded int reproduces the stored float so a future fractional value fails loud rather than silently truncating.

### Site 2 — Death-explosion fractional exponent (`0.3f`)

`kfDeathRadialPower = 0.3f` has no exact integer/half-integer rewrite, so this needs either a deterministic approximation or a formula restructure. Options, simplest first:

- **(2a) Restructure to avoid `pow`.** The expression `(pow(base, 0.3) - 1) * kfExplosionsRadius` is an artistic easing curve on `base ∈ [1, 2]` (since `(1 - fPercent) + 1`). Investigate replacing the `^0.3` ease with a `pow`-free curve of equivalent feel over `[1,2]` — e.g. a normalized `sqrt`-based or rational (Padé) ease, or a low-order polynomial fit of `base^0.3` on `[1,2]`. `sqrt` is an IEEE basic op (deterministic); a fixed-coefficient polynomial/rational is all basic ops. This is the smallest-footprint fix if a curve that reads the same visually is acceptable. **This is an artistic-feel change — flag for the user at grill** (it shifts death-explosion spread spacing slightly even beyond the determinism-driven drift).
- **(2b) A general `common::DeterministicPow(float fBase, float fExponent)` helper** computed as `exp2(fExponent * log2(fBase))` using fixed-coefficient minimax/Padé `log2`/`exp2` polynomials (basic IEEE ops only), mirroring the precedent set by the Padé `ExponentialDecay`/`ExponentialInterpolant` helpers (`MathUtils.h:189-208`) and `DeterministicSinCos`. Header-inline, `constexpr`-where-possible, with the same "deterministic transcendental replacement for CRC-fed paths" comment block. This is the reusable, general answer — pick it if (2a)'s curve restructure is rejected, or if any *other* fractional-pow sim site is anticipated. More code than (2a); justify by reuse, not by this single site.

Recommended: present **(2a) restructure** and **(2b) general helper** as the two real options at grill; if the artistic-curve change is unacceptable, fall to (2b). Do not author both. If (2b) lands, Site 1 may optionally route through it too, but the exact integer path (1a) remains preferred for the pushers (exact > approximate).

Accuracy bar for any approximation: the result feeds a CRC-validated sim path, so what matters is **bit-identical-across-CPUs**, not closeness to libm — any fixed polynomial of IEEE basic ops satisfies that. Closeness to the *old* libm value only governs how much the one-time replay/visual drift is (accepted).

## Critical files

- `Engine/Source/Frame/Collections/Pushers/PushersUpdate.cpp` — Site 1. `PushersInterpolate::ApplyPush` `:173` `XMVectorPow` → exact integer power (repeated `XMVectorMultiply`). No SOA/`SharedMembers()`/serialized-layout change (`pfPowers` stays a float field at `Pushers.h:78`); only the falloff math changes.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersCombat.cpp` — Site 2. `PlayersPostRender::SpawnDeathExplosions` `:476` `std::pow(..., kfDeathRadialPower)` → restructured curve (2a) or `common::DeterministicPow` (2b). `kfDeathRadialPower` constant at `:53`.
- `Common/Math/MathUtils.h` — home of any new helper: `DeterministicIntPow` (Site 1, if extracted) and/or `DeterministicPow` (Site 2, option 2b), placed beside `DeterministicSinCos` (`:219`) and the Padé `ExponentialDecay`/`ExponentialInterpolant` (`:189`), with the same determinism-rationale comment. Header-only; no `.cpp` and no vcxproj change (`MathUtils.h` already in the PCH path).
- `Documents/FloatingPointDeterminism.txt` — §5 currently calls out only sin/cos. Extend the "no libm transcendentals on sim paths" note to name `pow`/`XMVectorPow` and the deterministic replacement, so the doc matches the enforced reality.
- `Common/CLAUDE.md` — the `Math/MathUtils.h` bullet and the Determinism "Math helpers" sub-bullet list `ExponentialDecay`/`DeterministicSinCos`; add the new pow helper(s) (via `update-claude-docs` in the code-change process).
- `Common/Math/ConvexHull.h:22` carries the canonical "libm trig … derive via `common::DeterministicSinCos`" comment convention — mirror its phrasing for any new pow-determinism comment at the two call sites.

## Out of scope

- **Render-only / non-CRC `pow` sites.** `RenderTargetTextures.cpp:200` (`std::pow` in a smoke-trail mask build) and every GLSL-shader `pow` are render-path only and explicitly excluded (the shader `pow`-NaN hardening is its own landed/queued shader-review work; this plan is CPU sim paths only). Do NOT sweep them in.
- **Changing `pfPowers` SOA layout, type, or `SharedMembers()` membership.** Site 1 changes math only; the float field, its CRC inclusion, and serialized layout stay byte-identical (no `Frame::kiVersion` bump from Site 1).
- **Bumping `Frame::kiVersion` / any save-layout version.** No serialized layout changes at either site; only computed values shift. A version bump here would be wrong.
- **Re-baking `.pack`/`.manifest`/generated `.h` or regenerating committed replays.** Asset-path-hash CRCs are unaffected; the only resets are locally-recorded debug replays and in-flight sessions (accepted, same class as the sincos change). No artifact regen step belongs in this plan.
- **A general slider-driven pusher power.** Site 1 relies on the exponent being an integer constant today; do NOT add fractional-power plumbing speculatively (YAGNI). If that need arises later it routes through the Site-2 general helper.
- **Auditing other collections' falloff math** for stray `pow`/`exp` — that is a separate sweep; this plan fixes only the two sites the sincos sweep deferred.

## Acceptance criteria

- No `XMVectorPow`, `std::pow`, `powf`, or `_mm_pow_ps` remains on a CRC-fed sim path — confirmed by a post-change grep of the Pushers, Players, Spaceships, IslandTerrain, IslandChainPlacement, and NavBuild sim translation units.
- Site 1's replacement is **exact** for exponents 2 and 3 (repeated IEEE multiply), with an `ASSERT` (or `static_assert`-adjacent guard) that the stored power is integral, so a future fractional value fails loud.
- Any new `DeterministicPow`/`DeterministicIntPow` helper is header-inline in `MathUtils.h`, uses only IEEE basic ops (+, −, ×, ÷, sqrt) plus existing deterministic helpers — no libm call — and carries the determinism-rationale comment mirroring `DeterministicSinCos`.
- Both client and server still build (Pushers is unguarded; the helper is in the shared PCH path).
- `FloatingPointDeterminism.txt` §5 and the `Common/CLAUDE.md` math/determinism bullets name the pow replacement.

## Notes

- **Grill decisions:** (1) Site 1 — confirm `pfPowers` is provably integral today (it is: only `2.0f`/`3.0f` constants feed it) and that exact repeated-multiply (1a) is acceptable; pick "inline two-case" vs "extract `DeterministicIntPow`". (2) Site 2 — choose curve-restructure (2a, an artistic-feel change beyond pure determinism drift — user must accept the look shift) vs general `DeterministicPow` helper (2b). Do not build the general helper if the integer path covers Site 1 and (2a) covers Site 2.
- **Precedent to mirror:** `DeterministicSinCos` (`MathUtils.h:219`) and the Padé `ExponentialDecay`/`ExponentialInterpolant` (`MathUtils.h:189-208`) — same file, same comment style, same "basic IEEE ops only, CRC-safe" rationale. The convex-hull comment `ConvexHull.h:22` is the canonical call-site annotation pattern.
- **Both sites compile into both builds.** Pushers is ungated; `SpawnDeathExplosions` runs server-side sim and produces a CRC'd spawn position (the surrounding `Random*` jitter already runs unconditionally on both sides for parity). Neither fix is client-only.
- **Why this scored where it did:** small, localized math edits (two call sites + one header helper), but on the shared CRC path in both builds — a real determinism/desync source — hence Impact 4 and Risks 3, same risk class as the just-landed sincos/ConvexHull determinism work. Effort is held down by Site 1 being an *exact* integer rewrite (no approximation design) and Site 2 having a clean fall-back to the established Padé-helper precedent.
</content>
</invoke>

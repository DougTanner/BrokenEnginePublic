# MathUtils Correctness & Determinism Fixes

## Context (why)

`Common/Math/MathUtils.h` / `.cpp` are the shared `common::` XMVECTOR/math helpers consumed across DataPacker, Engine, and game code. A six-lens analysis surfaced a cluster of genuine correctness and determinism bugs that produce wrong output (not just style nits) for in-contract inputs:

- Color pack/unpack is not a round-trip and can corrupt adjacent channels.
- A heading helper returns NaN at the origin and folds the north/south hemispheres together.
- `RoundUp` invokes signed-overflow UB on the project's default `int64_t` type.
- Several degenerate-case guards use bitwise-exact / single-lane tests that either never fire or let near-singular inputs through to produce garbage rotations/directions.
- The Padé decay factor can flip the sign of a value it multiplies, contradicting its documented "never negative" guarantee.
- Two `RandomXYJitter` forms with the same intended distribution use different operation order on the RNG-consuming path (a determinism foot-gun).

Intended outcome: pack/unpack round-trips, degenerate-case guards behave correctly across all lanes and tolerance neighborhoods, no signed-overflow UB, and the math helpers stay deterministic and honest to their documented contracts. No new APIs, no defensive validation of caller-supplied data.

## Design (concrete changes)

### 1. `ColorToUint` — round instead of truncate, honor documented clamp [~15m]
`ColorToUint` in `MathUtils.cpp:167-174`. The header (`MathUtils.h:58-62`) documents this as the inverse of `ColorToVector` and states components are clamped to `[0,1]` before packing, but the implementation does neither:
- `static_cast<uint32_t>(255.0f * f)` truncates toward zero, so `ColorToUint(ColorToVector(c))` drifts down by 1 per channel for most bytes (e.g. byte 200 → `200/255` → `*255 = 199.999…` → 199). Add round-to-nearest: `static_cast<uint32_t>(255.0f * lane + 0.5f)` (lanes are non-negative after the clamp below, so `+0.5f` truncation == round).
- No clamp: a lane > 1.0 (accumulated/HDR color) or < 0 yields a value ≥ 256 / a huge wrap that overflows the 8-bit field and bleeds into the adjacent channel through the `<< 24/16/8` ORs. Saturate before packing to fulfill the documented contract and stop cross-channel corruption: apply `XMVectorSaturate(vecColor)` (then store / round). This is contract-fulfilment + corruption prevention, not defensive validation of caller data.
- Keep `ColorToVector` (`MathUtils.cpp:161-165`) unchanged; the fix makes the pair a true round-trip.

### 2. `RotationFromPosition` — replace `acos` with `atan2`, kill origin NaN [~20m]
`RotationFromPosition` in `MathUtils.cpp:11-16`. `acos(x / sqrt(x*x + y*y))` divides by zero at `x==0 && y==0` (position directly under the eye / at XY-origin) → `0/0 = NaN`, which then poisons downstream rotation matrices/quaternions (exactly the hazard `ValidateVector` exists to catch). It also collapses the range to `[0, π]`, mapping `(x, +y)` and `(x, -y)` to the same angle — the southern hemisphere is ambiguous. Replace with `std::atan2(y, x)`: single-valued over `[-π, π]`, deterministic, and well-defined at the origin (`atan2(0,0)==0`).
- **Caller-convention check required before changing**: `atan2(y,x)` returns a different value than the current `acos(x/len)` for the same input. Confirm every caller's expected zero-direction/sign convention against the project axis layout (W=-x, N=+y, E=+x, S=-y) and adjust the argument order/offset if a caller depends on the old (broken) convention. If no caller relies on the southern-hemisphere collapse, `atan2(y, x)` is the intended heading.
- Can read lanes via `XMVectorGetX/GetY` instead of materializing `XMFLOAT4A` (minor; do while touching).

### 3. `RoundUp` — eliminate signed-overflow UB [~20m]
The `RoundUp(T, T)` runtime overload (`MathUtils.h:122-126`) and the `RoundUp<T, MULTIPLE>` NTTP power-of-two/divide overload (`MathUtils.h:128-141`). `std::integral` admits signed `int64_t` (the project's default integer type). `iToRound + iMultiple - 1` (and `iToRound + MULTIPLE - 1`) is signed-overflow UB for large valid inputs; the bitmask path `(... ) & ~(MULTIPLE - 1)` on a signed `T` is wrong for negatives even without overflow (the NTTP path already casts to `make_unsigned_t<T>` only for the `has_single_bit` test, then does the arithmetic back on signed `T`).
- Fix: either constrain both overloads with `std::unsigned_integral T` (round-up of sizes/alignments is naturally unsigned), or compute internally on `std::make_unsigned_t<T>` and cast the result back. Prefer the `unsigned_integral` constraint if call sites already pass unsigned sizes; otherwise the internal-`make_unsigned_t` approach avoids churning call sites.
- Out of scope: the `iMultiple == 0` div-by-zero — caller-supplied validity is assumed per project rules; do not add a guard.

### 4. `ExponentialDecay` — clamp so it never goes negative (match its doc) [~10m]
`ExponentialDecay` in `MathUtils.h:173-177`. `(2 - x) / (2 + x)` is negative for `x = fDecayRate * fDeltaTime > 2`; the header comment claims "stable (never negative)." A large decay rate or a frame-time spike makes `velocity *= ExponentialDecay(...)` flip the velocity sign — a hard-to-trace gameplay bug. Clamp the result to `>= 0` (e.g. `std::max(0.0f, (2.0f - x) / (2.0f + x))`), which makes the documented guarantee true. Update the comment's inaccurate "~1% for x < 1.0" accuracy claim while here (the (1,1) Padé is ~9% off at x=1; the ~1% band is roughly x < 0.35). `ExponentialInterpolant` (`MathUtils.h:184-188`) is already safe for `x ≥ 0`; leave it. Skip the speculative `static_assert` on the domain (YAGNI).

### 5. `ComputeLeadPosition` — relative degeneracy epsilon [~20m]
`ComputeLeadPosition` in `MathUtils.cpp:85-131`, sim/CRC-path lead solver. The absolute thresholds `std::abs(fA) < 1.0e-6f` / `std::abs(fB) > 1.0e-6f` (`:93,96`) are scale-dependent: with kilometer-scale coordinates `fA`, `fB`, `fC` (speeds and distances squared) are large, so `1e-6` is effectively "exactly zero" and a small-but-nonzero `fA` takes the quadratic branch where `0.5/fA` is numerically unstable; at small scales it can wrongly trigger the linear branch. Use a relative epsilon scaled by `fProjectileSpeed * fProjectileSpeed` (or `max(|fA|, |fC|)`) for the `fA`/`fB` degeneracy tests so behavior is scale-invariant and deterministic.
- Optional (lower priority, only if numerical drift is observed): switch the two-root evaluation (`:108-109`) to the numerically-stable quadratic form `q = -0.5*(fB + copysign(fSqrt, fB)); t0 = q/fA; t1 = fC/q` to avoid catastrophic cancellation. This changes bit-results on a deterministic path, so gate it behind observed need.
- Do NOT add `ValidateVector` calls on the inputs (the report itself confirms the W handling is already correct — no bug).

### 6. `QuaternionFromDirection` — epsilon-test the anti-parallel singularity [~20m]
`QuaternionFromDirection` in `MathUtils.cpp:18-33`. Two bitwise-exact tests:
- `XMVectorEqual(vecOriginNormal, vecDirection)` + `XMVector4EqualInt(..., XMVectorTrueInt())` (`:20`) is exact all-lane equality; for computed/normalized directions it essentially never fires, so the identity fast-path is dead. Harmless but misleading.
- The real bug: the anti-parallel singularity is detected by `XMVector4EqualInt(XMVectorEqual(cross, XMVectorZero()), ...)` (`:26`) — an *exact* zero test on the normalized cross. A *nearly* anti-parallel input produces a tiny-but-nonzero cross that `XMVector3Normalize` turns into a garbage axis, which then flows to `XMQuaternionRotationNormal(vec, fAngle≈π)` → unstable/incorrect rotation. Replace the exact-zero test with a tolerance test on the pre-normalized cross length, e.g. `XMVector3LengthSq(rawCross) < eps` (compute `XMVector3Cross` once, test its squared length, normalize only on the non-singular path). Tolerance or remove the dead exact-equal identity path in the same edit.

### 7. `DirectionTo` — 3D coincidence test, not single-lane [~10m]
`DirectionTo` in `MathUtils.cpp:75-83`. `XMVectorGetX(XMVectorNearEqual(vecFrom, vecTo, g_XMEpsilon)) != 0.0f` reads only lane X of a per-lane mask, so two points equal in X but far apart in Y/Z are wrongly treated as coincident (returns `XMVectorZero()`), and the guard is asymmetric. Replace with a 3D distance test: `XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(vecTo, vecFrom))) < eps*eps` (or `XMVector3NearEqual` if available). Returning `XMVectorZero()` (W=0) on the coincident case stays correct for a direction.

### 8. `FloatToUnorm` — constrain to unsigned, fix 32/64-bit top-of-range overflow [~15m]
`FloatToUnorm` / `UnormToFloat` in `MathUtils.h:156-167`. `T` is unconstrained. For `T = uint32_t`/`uint64_t`, `static_cast<float>(std::numeric_limits<T>::max())` rounds up to `2^32` / `2^64`, so `fValue == 1.0f` (an in-contract input per the existing `ASSERT(fValue >= 0 && <= 1)`) computes `max()+1` and the float→integer cast overflows (UB). 8/16-bit are fine. Constrain both templates to `std::unsigned_integral T`; for 32/64-bit, compute the scale in `double` so the top of range maps exactly. Add `+ 0.5f` rounding to match the round-to-nearest fix in change 1 only if a caller uses these for color/quantization where the truncation bias matters (skip otherwise — YAGNI). Skip documenting `UnormToFloat`'s output range (doc-only).

### 9. `RandomXYJitter` — unify the two forms' operation order [~10m]
The compile-time `RandomXYJitter<JITTER>` (`MathUtils.h:78-82`) uses `Random(2.0f * JITTER, rRandomEngine)` while the runtime `RandomXYJitter(float, …)` (`MathUtils.h:85-88`) uses `Random<1.0f>(rRandomEngine) * 2.0f * fJitter`. Same intended distribution, different operation order on the RNG-consuming path → different bit-results feeding the same sim RNG stream (a determinism foot-gun per `Documents/FloatingPointDeterminism.txt` and the project's `/fp:strict` contract). Pick one scaling convention and apply it to both overloads so identical jitter magnitudes consume the RNG identically. (The `RandomAngleJitter` `-fMax + Random<2.0f>()*fMax` idiom is correct as-is — leave it.)

## Critical files
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Math\MathUtils.cpp` — changes 1, 2, 5, 6, 7
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Math\MathUtils.h` — changes 3, 4, 8, 9 (and the `ColorToUint` doc comment for change 1)

## Out of scope
- Adding any input validation / null / range / div-by-zero guards for caller-supplied data (`RoundUp(iMultiple==0)`, `MinAbs(NaN)`, `Ceil(Inf)`): project rule "assume parameters valid; do not add defensive validation."
- `Ceil` Inf/NaN/huge float→int64 UB: `consteval`-only; constant evaluation rejects UB at compile time, so there is no runtime bug for valid constants.
- Performance micro-optimizations with no behavior change: `ValidateVector` SIMD finiteness test, `ColorToVector` SIMD unpack, `RotateTowardsPercent` matrix-free 2D rotation, `RoundDown(float)` reciprocal form, `ComputeAabb` pack-by-value / `FXMVECTOR` convention — route to `/code-style-review`.
- Doc/comment-only nits (L8 wording, `InsideArea` layout comment, `CalculateArea` handedness comment, `DistanceSq` companion API) — cosmetic / YAGNI; not in this plan.
- Speculative API additions nobody uses (`DistanceSq`, domain `static_assert`s).
- Changing `ComputeLeadPosition`'s stable-quadratic form is gated behind observed numerical drift (it alters bit-results on a deterministic path).

## Acceptance criteria
- `ColorToUint(ColorToVector(c)) == c` for all `c` in `[0, 0xFFFFFFFF]` (exact round-trip); out-of-range XMVECTOR lanes saturate without bleeding into adjacent channels.
- `RotationFromPosition` returns a finite value for an at-origin position and distinguishes northern vs southern positions; existing callers produce the same headings they intended (convention verified).
- `RoundUp` compiles for the in-use integer types with no signed-overflow UB (or is constrained to unsigned).
- `ExponentialDecay` never returns a negative value for any `x ≥ 0`.
- `QuaternionFromDirection` and `DirectionTo` handle near-singular (nearly anti-parallel / nearly coincident) inputs without producing garbage axes / false coincidence.
- Both `RandomXYJitter` overloads consume the RNG with identical operation order for the same magnitude.
- Affected projects (DataPacker, BrokenEngineSandbox client + server) build clean.

## Notes (validation: what dropped & why)
Validated every Critical/High/Medium finding against the actual source (both files confirmed on-disk identical to the report's quoted lines) and against `Common/CLAUDE.md` (determinism contract, W-invariant) and the root "assume valid params / no defensive validation" rule.

Kept (became plan changes): H1 (ColorToUint truncation + documented-clamp corruption — correctness + cross-channel corruption for in-contract inputs), H3 (RotationFromPosition origin NaN + hemisphere collapse — genuine NaN-poisoning + ambiguous heading), H2 (RoundUp signed-overflow UB on the default `int64_t` type — kept the UB/unsigned-constraint core, dropped its sub-asks), H4-decay (ExponentialDecay sign flip vs documented "never negative"), M1-epsilon (scale-dependent absolute epsilon on the sim-path lead solver — determinism), M2 (anti-parallel exact-zero test lets near-singular inputs through), M3 (single-lane coincidence test), M5 (FloatToUnorm 32/64-bit top-of-range overflow UB for in-contract `1.0`), M6 (two RandomXYJitter forms differ in RNG operation order — determinism).

Dropped:
- **H2 sub-items** `iMultiple == 0` div-by-zero and "add ASSERT" — caller-supplied validity assumed; defensive-validation rule. Negative-input divergence folded into the unsigned-constraint fix.
- **H4 accuracy-claim correction** and **static_assert** — doc-only (folded the comment fix into the clamp change) / speculative (YAGNI).
- **M1 catastrophic-cancellation rewrite** — kept only as a gated optional; it changes bit-results on a deterministic path (risk) and is not yet shown to matter.
- **M1 W-lane ValidateVector ask** — report itself concludes the W handling is already correct; no bug.
- **M4 ValidateVector** — perf micro-opt of a debug-only ASSERT path; exact W compare already correct; no behavior bug → `/code-style-review`.
- **M5 sub-items** truncation-vs-round (conditional/YAGNI unless used for color) and UnormToFloat doc — doc/cosmetic.
- **M6 RandomAngleJitter idiom note** — correct as-is; cosmetic.
- **M7 Ceil/MinAbs edge cases** — `Ceil` is `consteval` (constant-eval rejects UB; no runtime bug); MinAbs NaN/signed-zero conceded acceptable, no defensive validation expected.
- **L1–L8** (all Low) — perf micro-opts, doc drift, convention notes → `/code-style-review`; the two load-bearing doc nits (ColorToUint clamp comment, ExponentialDecay "never negative") are folded into changes 1 and 4.
- No FMA/transcendental-ordering determinism concerns were raised that the project's `/fp:strict` + FMA3-disabled posture already moots; none to drop on that basis.

Caller enumeration (all call sites confirmed):
- `ColorToUint`: only internal caller is `ColorLerp`; `ColorToVector` round-trips through it. No external callers — change 1 has a tiny blast radius (color blends only).
- `RotationFromPosition`: single caller `BillboardsRender.cpp:91` — `XM_PI + XM_PIDIV2 + RotationFromPosition(normalize(particlePos))` (client/graphics only, NOT sim/CRC). The argument is a normalized particle position; the implementer MUST verify the billboard's expected sprite orientation against `atan2(y,x)` and re-tune the `XM_PI + XM_PIDIV2` offset if the convention shifts. This is render-path, so no determinism risk, but it is visible.
- `RoundUp`: ~40 call sites, ALL pass non-negative sizes/alignments as `int64_t`/`uintptr_t`/`LONG`/`VkDeviceSize` (DataPacker export sizes, `CollectionMemory` capacities, `FileManager` offsets, `DataFile.h:311` `kiChunkDataOffset`). No call site passes a negative value, so the UB is latent today — but the fix (unsigned constraint or internal `make_unsigned_t`) is cheap insurance and the `int64_t`/`LONG` instantiations confirm the signed-type path is actively used. Constraining to `std::unsigned_integral` would break the many `int64_t`/`LONG` sites, so the internal-`make_unsigned_t` approach is the safer fix here.
- `FloatToUnorm`/`UnormToFloat`: ONLY instantiation in the whole codebase is `<uint16_t>` (`DataPacker/Source/Texture.cpp:132,474`). The 32/64-bit top-of-range overflow (M5) is therefore NOT currently reachable — change 8 becomes a no-op safety constraint (`std::unsigned_integral` + the double-scale guard), low value. Consider deferring change 8 or downgrading it to a `static_assert` guard only.
- `ComputeLeadPosition`: 2 callers in `PlayersCombat.cpp:140` / `PlayersRender.cpp:208`, both on the sim/render path at kilometer scale (`kfPlayerBlastersSpeed`, spaceship positions) — confirms M1's scale-dependent-epsilon concern is real for the actual world scale.
- `QuaternionFromDirection`: no direct game callers (only `RotationMatrixFromDirection` wraps it, which also has no callers found) — change 6 is latent/defensive; lower priority than the report's M2 implies. `DirectionTo`: one sim caller `PlayersCombat.cpp:423` (target-direction). The jitter helpers are heavily used on the sim RNG path (`PlayersCombat`, `Spaceships`, `Blasters`, `Missiles`, `Explosions`), confirming M6's determinism relevance — though note the two *literal* `RandomXYJitter` overloads have NO direct callers (all use goes through `RandomPositionJitter`/`RandomDirectionJitter`, which call the compile-time `RandomXYJitter<JITTER>` form, and the runtime `RandomXYJitter(float,…)` reached only via `RandomPositionJitter(float,…)` at `ExplosionsSpawn.cpp:201`). Both forms ARE reachable on the sim path, so the operation-order divergence is a live determinism foot-gun if a magnitude is ever expressed both ways.

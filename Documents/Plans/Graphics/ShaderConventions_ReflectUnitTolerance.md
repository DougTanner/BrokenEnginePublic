# Document `reflect(unit, unit)` Unit-Length Identity in Shader Conventions

## Context

GLSL's `reflect(I, N)` returns `I - 2 * dot(N, I) * N`. When both `I` and `N` are unit vectors, the result is also unit-length by algebraic identity:

```
|reflect(I, N)|^2 = |I - 2(N.I)N|^2
                  = |I|^2 - 4(N.I)(I.N) + 4(N.I)^2 |N|^2
                  = 1 - 4(N.I)^2 + 4(N.I)^2
                  = 1
```

So callers that pass two pre-normalized vectors into `reflect()` and then immediately consume the result as a direction may omit any outer `normalize()` wrapper. This saved one `rsqrt + 3 muls` per fragment in a previous shader perf pass at three call sites:

- The skybox-reflection sample feeding `textureLod(skyboxSampler, ...)` in `Water.frag:195` and the specular reflection vector at `Water.frag:200`.
- The reflection direction passed to `Specular(...)` in `HexShield.frag:40`.
- The IBL reflection vector at `Model.frag:252` (`reflection = -reflect(v, n)` where both `v` and `n` are pre-normalized at `Model.frag:249` / `Model.frag:248`).

Without this convention documented, the next contributor reading any of those sites is likely to defensively re-add `normalize()` — the call looks "naked" without it — and silently regress the perf win. The risk is especially high because the inputs are themselves normalized two-or-three lines above, making the "is this already unit?" check non-local.

## Design

### Add a bullet to `Engine/Data/Shaders/CLAUDE.md`

Under the existing `## Architecture Notes` section, add a new bullet:

> **`reflect(unit, unit)` is unit by identity**: GLSL `reflect(I, N) = I - 2*dot(N,I)*N` preserves unit length when both inputs are unit vectors (algebraic proof: `|reflect|^2 = |I|^2 - 4(N.I)^2 + 4(N.I)^2 * |N|^2 = 1`). Callers that pre-normalize both inputs may consume the result directly as a direction with no outer `normalize()`. A leading sign flip (`-reflect(...)`) or a single-axis sign flip via componentwise multiply (`reflect(...) * vec3(-1, 1, -1)`) preserves unit length too — these are sign changes, not magnitude changes. Removing the redundant `normalize()` saves one `rsqrt + 3 muls` per fragment per call site (currently applied at `Water.frag:195`, `Water.frag:200`, `HexShield.frag:40`, `Model.frag:252`).

### Add a `glsl-review` heuristic note

Add a one-line heuristic the `glsl-review` skill can pattern-match on:

> Flag `normalize(reflect(a, b))` where both `a` and `b` are demonstrably unit at the call site as a redundant `normalize()`. Same for `normalize(-reflect(unit, unit))` and `normalize(reflect(unit, unit) * vec3(+/-1, +/-1, +/-1))`. The wrapper is a no-op; the inner expression is already unit. Conversely, flag a *removal* of `normalize()` around `reflect(a, b)` if either input cannot be proven unit at the call site.

The heuristic file lives wherever the `glsl-review` skill stores its checklist — at execution time, locate the skill's reference list and append the bullet. If the skill has no central checklist file, embed the heuristic as a comment near the convention bullet in `Engine/Data/Shaders/CLAUDE.md` so `glsl-review` invocations naturally pick it up from the architecture-notes context.

## Critical files

- `Engine/Data/Shaders/CLAUDE.md` — add the bullet under `## Architecture Notes`.
- `Engine/Data/Shaders/Water/Water.frag:195`, `Water.frag:200` — example call sites for the bullet.
- `Engine/Data/Shaders/HexShield/HexShield.frag:40` — example call site.
- `Engine/Data/Shaders/Model/Model.frag:252` (the `-reflect(v, n)` line where `v` and `n` come from `normalize(...)` two and four lines above) — example call site for the sign-flip variant.
- `glsl-review` skill checklist (path TBD at execution time) — add the regression heuristic.

## Out of scope

- Auditing other GLSL identities for similar documentation gaps (e.g., `cross(unit, unit)` is *not* unit; `mix(unit, unit, t)` is *not* unit). This plan documents one identity; broader audits land separately if the pattern recurs.
- Re-removing or re-adding `normalize()` at any specific call site. This plan is documentation-only — the existing optimized call sites stay as they are; the bullet captures the rule so they survive future drive-by edits.
- Performance measurement. The `rsqrt + 3 muls` figure is the cost of `normalize(vec3)` on every shader target and is well-established; this plan does not require re-measuring.
- DirectXMath / HLSL parallels. The identity holds in any reflection formulation, but the engine's C++ side uses different vector types and the same documentation friction does not apply.

## Acceptance criteria

- `Engine/Data/Shaders/CLAUDE.md` contains a bullet under `## Architecture Notes` stating the identity, the sign-flip variant, and naming the four current call sites.
- The bullet includes the four-step algebraic proof inline or as a one-line reference so the reader does not have to re-derive it.
- The `glsl-review` skill has a one-line heuristic that will flag either direction of regression (adding a redundant `normalize` or removing a load-bearing one).
- No shader source changes — diff is `CLAUDE.md` only (plus optionally one file owned by the `glsl-review` skill).

## Notes

- Effort 1, Impact 2, Risks 0, Score -1. Tier Quick Win.
- Documentation-only; the regression cost (one `rsqrt + 3 muls` per fragment per regressed site) compounds across the visible-area water and full-screen model passes, so the impact is "modest perf preservation across many fragments" rather than "single-shot one-time fix" — that is why Impact is 2 not 1.
- Risk is 0: pure docs, no behavior change.

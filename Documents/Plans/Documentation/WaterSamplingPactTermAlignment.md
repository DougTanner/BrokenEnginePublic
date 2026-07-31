<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-31T14:01:04.000Z","dependsOn":[]} -->
# Align the water precision-sampling term with "contract"

## Context

The plain-language word sweep changed `Engine/Data/Shaders/Water/AGENTS.md` to say "contract" everywhere ( `:5` "Geometry and Shore Contracts", `:11` "Precision Contracts", `:14` "integer-product modulus contract", `:17` "Lighting Contracts"). The word it replaced, "pact", survives in four places the sweep declared out of scope, and three of those four point the reader at that same AGENTS.md by name:

- `Documents/Features/Graphics/WaterSunGlitter.md:14` — "precision-safe UV pact"
- `Documents/Features/Graphics/WaterSunGlitter.md:23` — "MUST follow the precision-safe sampling pact (`fract()`-wrapped UV + `textureGrad`, CPU-reduced origin — see Water/AGENTS.md)"
- `Documents/Features/Graphics/WaterFoam.md:31` — "MUST follow the precision-safe sampling pact (Water/AGENTS.md)"
- `Documents/Features/Graphics/WaterCaustics.md:29` — "MUST follow the precision-safe sampling pact"
- `Engine/Data/Shaders/Water/Water.frag:90` — "// Color (noise with precision-safe UV — same pact as SAMPLE_NORMAL_PRECISE above)."

So a reader following "the precision-safe sampling pact (Water/AGENTS.md)" now lands on a document that never uses that word. One concept has two names, which the repository directive forbids.

Originating gap: `Documents/Plans/Documentation/FormalWordSwaps.md` scoped itself to agent guidance files and explicitly excluded `Documents/Features/` and code. Fixing these five sites was outside that boundary, not an acceptance failure of it.

## Design

Replace "pact" with "contract" at the five sites listed above, changing nothing else in the surrounding sentences. `Water.frag:90` is a comment; the shader's instructions are untouched.

## Critical files

- `Documents/Features/Graphics/WaterSunGlitter.md`
- `Documents/Features/Graphics/WaterFoam.md`
- `Documents/Features/Graphics/WaterCaustics.md`
- `Engine/Data/Shaders/Water/Water.frag`
- `Engine/Data/Shaders/Water/AGENTS.md` — read-only; it already says "contract" and is the term's owner.

## In scope

- `Documents/Features/Graphics/WaterSunGlitter.md` — the two "pact" occurrences (`:14` and `:23` today; re-derive, line numbers move).
- `Documents/Features/Graphics/WaterFoam.md` — the single "pact" occurrence at `:31`.
- `Documents/Features/Graphics/WaterCaustics.md` — the single "pact" occurrence at `:29`.
- `Engine/Data/Shaders/Water/Water.frag` — the word "pact" inside the comment at `:90` only.

## Out of scope

- Every other word in those five sentences, and every other line of the four files.
- `Engine/Data/Shaders/Water/AGENTS.md` and any other AGENTS.md or SKILL.md — already swept.
- All GLSL statements, uniforms, macros, and sampling behavior. This Plan changes one comment's wording and nothing a compiler acts on.
- Other formal-word swaps anywhere in `Documents/Features/`, `Documents/Plans/`, or `Documents/Investigations/` — see Coordination.
- Any C++ change, including `WaterUniforms.cpp`, which the prose cites but this Plan does not touch.

## Risk tier

Tier 1. Trigger: documentation wording plus one shader comment; no public signature, invariant, determinism/CRC, serialization, wire, threading, or behavior exposure. The only build effect is that `Water.frag` is recompiled because its bytes changed.

## Acceptance criteria

- A repository grep for the word `pact` (word-boundary, case-insensitive) across `Documents/Features/Graphics/` and `Engine/Data/Shaders/Water/` returns nothing.
- The `Water.frag` diff contains only comment text; no line of GLSL code changes.
- The client shader build succeeds.

## Coordination

Sweeping the planning and investigation write-up trees for formal words is now a tracked Plan of its own, `Documents/Plans/Documentation/PlanningTreeJargonSweep.md`. It covers a different set of words and files. This Plan's five sites are already handled here and must not be redone. No ordering requirement in either direction.

<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-01T19:00:52.062Z","dependsOn":[]} -->
# Align the Two C++ Water Precision Comments on the Word "Contract"

## Context

`Engine/Data/Shaders/Water/AGENTS.md` names the water sampling and precision rules "contracts" (`:5` "Geometry and Shore Contracts", `:11` "Precision Contracts", `:14` "integer-product modulus contract", `:17` "Lighting Contracts"). `WaterSamplingPactTermAlignment.md` then replaced the older word "pact" with "contract" at the five sites it owned: three `Documents/Features/Graphics/Water*.md` documents and the comment at `Engine/Data/Shaders/Water/Water.frag:90`, which now reads `// Color (noise with precision-safe UV — same contract as SAMPLE_NORMAL_PRECISE above).`

That Plan explicitly excluded C++ from its scope, so two C++ comments still call the same concept a "pact". Both point the reader at the shader that no longer uses that word:

- `Engine/Source/Ui/WaterWrappersBase.cpp:130` — `// Step 0.1 keeps mult*10 integer for the Water.frag fract()-wrap precision pact (see Water.frag color-noise UV block).`
- `Engine/Source/Graphics/Render/WaterUniforms.cpp:86` — `// wrap for any φ (precision pact intact).`

A reader following `WaterWrappersBase.cpp:130` to the "Water.frag color-noise UV block" lands on a comment that calls the same rule a contract. One concept therefore still has two names in the repository, which the root `AGENTS.md` "one term per concept" directive forbids.

`Documents/Plans/Documentation/PlanningTreeJargonSweep.md` does not cover these sites: its scope is tracked `*.md` under `Documents/Plans/`, `Documents/Investigations/`, and `Documents/Features/`, and its `## Out of scope` section names "C++, GLSL, script, and `.txt` files" outright.

## Design

Replace the word "pact" with "contract" at the two C++ sites listed above, changing nothing else in either sentence. Both are comments; no executable line, wrapper default, bound, snap grid, or uniform value changes.

Resulting text:

- `WaterWrappersBase.cpp` — `// Step 0.1 keeps mult*10 integer for the Water.frag fract()-wrap precision contract (see Water.frag color-noise UV block).`
- `WaterUniforms.cpp` — `// wrap for any φ (precision contract intact).`

Line numbers move; re-derive both sites with a word-boundary, case-insensitive grep for `pact` across `Engine/Source/` before editing.

## Critical files

- `Engine/Source/Ui/WaterWrappersBase.cpp` — the single "pact" occurrence in the comment above `gWaterColorNoiseMultiplierOne` (`:130` today).
- `Engine/Source/Graphics/Render/WaterUniforms.cpp` — the single "pact" occurrence in the reduced-time comment block above `sdReducedTimeOneX` (`:86` today).

## In scope

- The word "pact" -> "contract" inside those two comments only.

## Out of scope

- Every other word in those two comments, and every other comment in either file.
- Any executable C++ line, including the `0.1f` snap grid on `gWaterColorNoiseMultiplierOne` and the reduced-time accumulators the second comment describes.
- `Engine/Data/Shaders/Water/**`, `Documents/Features/Graphics/Water*.md`, and every other site owned by `WaterSamplingPactTermAlignment.md`.
- Any other C++ file, header, script, or document; renaming identifiers; any other terminology sweep.

## Risk and invariants

Change Workflow Tier 1. Trigger: mechanical comment-only work in two `.cpp` files, with no public signature, invariant, determinism/CRC, serialization, wire, threading, or trust-boundary exposure — the compiled bytes are unchanged.

Invariants:

1. Both files still compile in the client build; `WaterWrappersBase.cpp` compiles into both builds, so neither edit may touch a preprocessor guard.
2. No executable statement, literal, or declaration changes — the diff is comment text only.
3. The two comments keep their existing cross-references intact: `WaterWrappersBase.cpp` still names the Water.frag color-noise UV block, and `WaterUniforms.cpp` still describes the same integer-product wrap rule.

## Acceptance criteria

- A word-boundary, case-insensitive grep for `pact` across `Engine/Source/` returns nothing.
- `git diff` shows exactly two changed lines, both comment lines, in exactly the two files named above.
- The changed C++ compiles (client and server for `WaterWrappersBase.cpp`, client for `WaterUniforms.cpp`).

## Notes

No `dependsOn` edge is recorded. The prerequisite rename in `Engine/Data/Shaders/Water/Water.frag` and the three `Documents/Features/Graphics/Water*.md` documents is the deliverable of `WaterSamplingPactTermAlignment.md`, which was executed before this Plan was written; a dependency edge to a Plan that is completing would be a stale edge from the moment it was added. If that work were ever reverted, this Plan's premise should be rechecked before implementing.

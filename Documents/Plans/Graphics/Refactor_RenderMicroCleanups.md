# Refactor: Render Micro-Cleanups

## Context

Source: /external-refactor-clean on `Engine/Source/Graphics/Render` (non-recursive). Small mechanical in-function
items: a per-frame reconstruction of three Perlin permutation tables, two unnamed magic constants on per-frame
paths, and two trivia. Hot-path allocation discipline is otherwise fully compliant (no heap, no `LOG` float specs —
the directory has no `LOG` calls at all).

## Design

### Engine/Source/Graphics/Render/MainUniforms.cpp
- `RenderFrameMain` constructs `siv::BasicPerlinNoise<float> perlinRoll(0)/perlinPitch(1)/perlinYaw(2)` every frame
  (`:254-256`); each construction reshuffles a 256-entry permutation table. The seeds are fixed, so the tables are
  identical every frame — hoist to `static const` (function-local) objects. Output bit-identical; saves three
  table shuffles per frame. [~5m]
- Name/comment the wave-cull magic `if (i < 64 && (i % 3) == 0)` (`:344-347`) — zeroing every third low-wave
  amplitude below index 64 is unexplained; add named constants (e.g. `kiWaveCullModulo`/`kiWaveCullLimit`) or a
  comment stating the tuning intent. [~5m]
- `#endif // BT_CLIENT` (`:402`) → `#endif // defined(BT_CLIENT)` to match the other five files. [~1m]

### Engine/Source/Graphics/Render/WindUniforms.cpp
- `fWindTimeScale = fDeltaTime * 60.0f * gWindTimeScale.Get()` (`:49`) — the `60.0f` is an unnamed
  frame-rate-normalization reference (wind shader params are tuned against a 60 fps step) that the wind shaders
  implicitly depend on. Name it (file-local `kfWindReferenceFps` or similar) with a one-line comment. [~5m]

### Engine/Source/Graphics/Render/GlobalUniforms.cpp
- Empty `else if` branch in the stretch chain (`:209-211`) — intentional "midday: no stretch" window with no
  comment. Add the one-line comment inside the branch (do not restructure the chain — the explicit window
  documents the angle coverage). [~1m]

## Critical files
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp`
- `Engine/Source/Graphics/Render/MainUniforms.cpp`
- `Engine/Source/Graphics/Render/WindUniforms.cpp`

## Out of scope
- The `// DT: TEMP` water Z offset chain (`GlobalUniforms.cpp:528`) — owned by
  `Ui/Refactor_WaterZOffsetTempDecision.md`.
- The stale "terrain no longer uses indirect draws" comment (`MainUniforms.cpp:176-177`) — owned by
  `Common/StaleCodeCommentsSweep.md` item.
- Function decomposition — `Graphics/Refactor_RenderFunctionDecomposition.md`.
- The `common::RandomEngine` per-frame constructions in the wave loops — trivially cheap stack objects, correctly
  re-seeded for stable per-frame patterns; not a defect.

## Acceptance criteria
- Client builds clean; rendering bit-identical (Perlin seeds unchanged, constants renamed not revalued).

## Notes
- No determinism/CRC/network/`kiVersion` exposure — client render path only; all items value-preserving.
- The Perlin hoist is safe for the documented once-per-frame entry-point contract: `static const` initialization
  is thread-safe and the objects are read-only after construction.

## Verification Notes

Verified 2026-06-11 against current source.
- **Dropped: the `XMFLOAT4`→`XMFLOAT4A` switch.** The SDK's `XMFLOAT4A` is
  `struct XMFLOAT4A : public XMFLOAT4 { using XMFLOAT4::XMFLOAT4; };` (inherited ctors exclude the base copy
  ctor), so it has no constructor or `operator=` taking `const XMFLOAT4&`. The shadow/lighting latches assign
  *from* plain-`XMFLOAT4` layout fields (`sf4PreviousShadowArea = rGlobalLayout.f4ShadowArea` at
  `GlobalUniforms.cpp:276`/`:285`; lighting equivalents `:390`/`:399`), so the swap does not compile as a plain
  type change — it would need explicit per-site conversions, pure churn for what the item itself conceded was not
  a perf defect (the values are never `XMLoadFloat4`-ed). The Smoke pair alone is drop-in but keeping it while the
  mirrored shadow/lighting latches stay `XMFLOAT4` would reduce consistency, not improve it.
- Retained items re-verified: Perlin ctor confirmed to reshuffle per construction
  (`PerlinNoise.hpp:427-429` → `reseed(seed)`) and `octave1D_01` is `const noexcept` (`:203`) — hoist is
  bit-identical; wave-cull magic at `MainUniforms.cpp:344-347`; `#endif // BT_CLIENT` at `:402` (the other five
  files use `defined(BT_CLIENT)`); `60.0f` at `WindUniforms.cpp:49`; empty `else if` at `GlobalUniforms.cpp:209-211`
  (comment-free midday window). "No `LOG` calls in the directory" confirmed by grep.
- Scoring: item count drops from five to four, all remaining items are comment/rename/hoist-level — if the
  original row assumed the `XMFLOAT4A` site work, consider Effort 1 / Impact 1-2 / Risks 0-1 (Quick Win) on
  re-score; otherwise unchanged.

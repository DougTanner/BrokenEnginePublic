# Shader Review — Misc (top-level Shaders/)

Files: `Log.vert`, `Clear.frag`, `DebugTexture.frag`

## PASS

- `Engine/Data/Shaders/Clear.frag` — single-line color write from push constant. Minor note: `iInInstanceIndex` and `f2InTexcoord` are declared but unused; if the paired vert only feeds this frag, drop them.

## EXECUTED (prior session)

- `Log.vert:38` — `gl_Position.w = 0.0f` perspective divide-by-zero fixed (mirrors `QuadsFullscreen.vert:25` NDC formula).
- `Log.vert:33` — exact-float `f2InQuadVertex == vec2(0.0f)` replaced with `gl_VertexIndex == 0`.
- `DebugTexture.frag` — `f4OutColor` initialized to `vec4(0,0,0,1)` at function entry; `C2 = (a * P) / max(P - S1, 1e-6f)` joint-constraint guard; `pow(max(f4Scaled, 0.0f) / m, vec4(c))` base clamp.
- Prior concerns about `DebugTexture.frag:35` `pow(fPassCount, -scale)` and `:44` `/a` are wrapper-guaranteed safe; see comments in `Engine/Source/Ui/WrapperBase.cpp`.

## REMAINING

### `Engine/Data/Shaders/Log.vert`

Correctness:
- lines 28-29 — `iOutInstanceIndex` and `f2OutTexcoord` are declared as outputs but never written in `main()` (the prior planning assumption that they were written at lines 21/23 was wrong; the current file assigns nothing to them). If any paired fragment stage reads these, values are undefined. Decision needed: either assign them (`iOutInstanceIndex = gl_InstanceIndex; f2OutTexcoord = f2InQuadVertex;`) or remove both declarations. Non-blocking for the `debugPrintfEXT` use case if no fragment stage reads them.

Vulkan/API:
- lines 2 / 35 — `debugPrintfEXT` requires `VK_KHR_shader_non_semantic_info`; strip or guard before shipping.

### `Engine/Data/Shaders/DebugTexture.frag`

Performance:
- line 28 — unconditional `texture()` sample is dead for VisibleArea/DepositDirectionCombined branches (lines 74, 95). Move into branches that use it, or accept the negligible cost.
- line 123 — re-samples `debugTextures[iIndex]` with the same UVs as line 28; reuse `f4Sample`.

Vulkan/API:
- lines 12-15 — sized sampler arrays on set 1 are fine (per-pipeline role matches). **Confirm `fDebugTextureIndex` is dynamically uniform per draw** (C++ side — verify before deciding); if not, add `nonuniformEXT(iIndex)` at lines 28, 123-125 even though the arrays are sized.
- Debug-view only: `fTotal > 0.0f` guards at lines 55-56, 64-65, 116-117, 130-131 miss negative F16 EWNS sums → divide produces wrong ratio (no NaN). Pre-existing; accept for debug overlay.

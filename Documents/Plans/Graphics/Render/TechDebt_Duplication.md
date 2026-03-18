# Tech Debt: Code Duplication

Source: /external-tech-debt on Engine/Source/Graphics/Render

## Changes

### Engine/Source/Graphics/Render/SmokeUniforms.cpp + WindUniforms.cpp
- Extract shared spread quad offset computation into a file-static helper in a shared location (e.g. Render.h as an inline or a new small .cpp) [~15m]
  - SmokeUniforms.cpp lines 73-78 and WindUniforms.cpp lines 68-73 are near-identical
  - Both compute fXOffset/fYOffset from previous vs current area, then populate an AxisAlignedQuadLayout with vertex rect, texture rect, and zeroed params
  - Differs only in buffer source (mSmokeSpreadStorageBuffers vs mWindSpreadStorageBuffers)
  - Helper signature: `void WriteSpreadQuad(const XMFLOAT4& rPreviousArea, const XMFLOAT4& rCurrentArea, shaders::AxisAlignedQuadLayout& rQuad)`
  - Note: the `sf4Previous* = rGlobalLayout.f4SmokeArea;` assignment after the quad write remains at each call site (each manages its own static state)

## Verification Notes
- Duplication confirmed: both sites compute identical offset math and write identical quad layout fields
- Helper abstracts ~5 lines per call site; marginal but consistent with DRY directive
- Wind uses `sf4PreviousWindArea` and smoke uses `sf4PreviousSmokeArea`, but both reference `rGlobalLayout.f4SmokeArea` as the current area -- the helper correctly parameterizes this via `rCurrentArea`
- Line numbers adjusted: SmokeUniforms.cpp 73-78, WindUniforms.cpp 68-73 (line 79/74 are the previous-area assignment, kept at call site)

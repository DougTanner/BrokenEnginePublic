# Rename `FromGamma`, prune the `bFromGamma=true` branches

## Context

The Color/Normals color-space cleanup (May 2026) left the `common::FromGamma()` helper at `Common/MathUtils.cpp:133` and the `bFromGamma=true` branches in `DataPacker/Source/Texture.cpp` orphaned on the bake hot path. The helper's name is misleading — `FromGamma(x) = pow(x, 1/2.2)` is a **linear → sRGB encoding**, not "from gamma to anything". The bug it caused (double sRGB-encoding of Gaea2's Color.exr) is one mis-set bool away from recurring: anyone setting `bFromGamma=true` on a future `Texture` ctor call will silently re-introduce the same washed-out output.

## Recommended approach

1. Rename `common::FromGamma` → `common::LinearToSrgb` (or delete entirely if no future caller materializes after the audit below).
2. Audit every remaining `bFromGamma` parameter site:
   - `DataPacker/Source/Texture.h:28` (`Texture` ctor parameter)
   - `DataPacker/Source/Texture.cpp:109-119` (`kFloat32` branch with both `if (bFromGamma)` arms)
   - `DataPacker/Source/Texture.cpp:219-238` (EXR branch with both arms)
   - All call sites — the only remaining ones after the cleanup are all `bFromGamma=false`.
3. If no caller passes `true` and none plausibly will (game-Z heightmap, color-mask images, etc.), delete the parameter, delete both arms' `if (bFromGamma)`, simplify each branch to the `else` arm.
4. If a future caller might want linear→sRGB encoding for a different reason, keep the helper but rename it for clarity and document the call site.

## Critical files

- `Common/MathUtils.h` and `Common/MathUtils.cpp` — `FromGamma` declaration + definition
- `DataPacker/Source/Texture.h` — `Texture` ctor parameter
- `DataPacker/Source/Texture.cpp` — `kFloat32` and EXR branches

## Verification

Build DataPacker and run the bake; outputs must remain byte-identical (no caller currently sets `bFromGamma=true`, so the simplification is a no-op at runtime). Confirm via the BC7 file mtime/size on `Engine/Data/Islands/02/Color.BC7_UNORM_BLOCK` after a forced re-bake (kiBakeVersion bump) — bytes must match the post-this-session output.

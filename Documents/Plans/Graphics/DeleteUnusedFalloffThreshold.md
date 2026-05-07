# Delete unused `kFalloffThreshold` constants

## Context
- File: `Engine/Data/Shaders/ShaderLayoutsBase.h:675-676`
- Constants defined: `kFalloffThreshold = 0.45f` and `kFalloffThresholdInv = 1.0f / kFalloffThreshold`
- Repo-wide `Grep` confirms both names are referenced only at their definition site. No GLSL shader, no C++ source, and no header consumes either constant.
- Surfaced during the `01_Model.md` review session, when the new `kf`-prefix naming convention was added to `Engine/Data/Shaders/CLAUDE.md` and these constants stood out for using bare `k` (no type prefix). Investigation showed they are dead — deletion is simpler than rename.

## Design
Delete the two `CONSTEXPR float` lines. Nothing else to change.

## Critical files
- `Engine/Data/Shaders/ShaderLayoutsBase.h:675-676` — delete both lines and the surrounding blank line if it leaves a doubled empty section.

## Out of scope
- Renaming any other constant in `ShaderLayoutsBase.h` to align with the `kf`/`ki`/`ke`/`kb` Hungarian convention. Audit each constant's usage on its own merit; do not sweep names just because new ones were added in the `01_Model.md` session.
- Sweeping other potentially-unused constants in this file. The audit only flagged these two; any broader dead-constant pass is its own plan.

## Acceptance criteria
- Both `kFalloffThreshold` and `kFalloffThresholdInv` definitions are removed from `Engine/Data/Shaders/ShaderLayoutsBase.h`.
- Repo-wide `Grep` for either identifier returns zero matches.
- DataPacker `Release` build succeeds (verifies the C++-side `inline constexpr` removal compiles) and the BrokenEngineSandbox client+server builds still link.

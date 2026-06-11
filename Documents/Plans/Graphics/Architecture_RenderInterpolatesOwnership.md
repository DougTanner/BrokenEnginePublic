# Architecture: mRenderInterpolates Ownership

## Context

Source: /external-architecture-review on `Engine/Source/Graphics` (non-recursive). `Graphics` stores
render-interpolation state it never touches: the member `mRenderInterpolates`
(`std::unordered_map<GridCoord, game::FrameInterpolate>`, `Graphics.h:103`) is populated/pruned by
`GameBase::Render` (`Engine/Source/GameBase.cpp:353-386`) plus a one-time boot-path population in
`Main.cpp:212-218` (origin-coord `AllocateAndCopy` + camera read before the first present loop), consumed by
the camera update (`GameBase.cpp:397`), and then passed *back into* `Graphics::RenderMainPresentAcquire` as
a parameter — both call sites (`GameBase.cpp:418`, `Engine/Source/Main.cpp:226`) pass
`gpGraphics->mRenderInterpolates`. `Graphics`' own code never reads or writes the member (it only receives
the `rRenderInterpolates` parameter). The state lives on one object, its lifecycle on another, and the
function receives as a parameter what its own object already owns.

It is also the most fragile dependency edge in the directory: an engine header holds a **by-value** member
of a game type against only an in-file forward declaration (`Graphics.h:3-8`), compiling solely because
`Pch.h` includes the game's `Frame/Frame.h` (`Pch.h:96`) before `Engine.h` (`Pch.h:97`) — a hidden,
PCH-ordering-load-bearing dependency of an engine header on a game type's complete definition.

## Design

### Engine/Source/GameBase.h / GameBase.cpp
- Move the `mRenderInterpolates` member from `Graphics` to `GameBase` — the sanctioned engine→game hub that
  already owns the render-frame lifecycle (it populates, prunes, and forwards the map). [~15m]
- Add an explicit `#include` of the game `Frame/Frame.h` in `GameBase.h` (alongside its existing game
  `Graphics/Camera.h` include) so the complete-type requirement is declared where the by-value member lives,
  instead of riding PCH ordering. [~5m]
- Update all `gpGraphics->mRenderInterpolates` references (grep repo-wide at execution; known:
  `GameBase.cpp:353,376,377,397,418`, `Main.cpp:216,218,226`) to the new home. [~15m]

### Engine/Source/Graphics/Graphics.h
- Delete the `mRenderInterpolates` member (`:103`). Keep the `game::FrameInterpolate` forward declaration
  (`:3-8`) — the `RenderMainPresentAcquire` const-reference parameter (`:69`) only needs an incomplete type
  in the header. The signature itself stays (the map is genuinely an input to the render). [~5m]

## Critical files
- `Engine/Source/Graphics/Graphics.h`
- `Engine/Source/GameBase.h`, `Engine/Source/GameBase.cpp`
- `Engine/Source/Main.cpp`
- Any other `mRenderInterpolates` consumers found by grep (e.g. the game `Camera::Update` read path)

## Out of scope
- Dropping the `rRenderInterpolates` parameter in favor of a direct member read inside
  `RenderMainPresentAcquire` — keeping the map as an explicit input preserves the render function's
  data-in/data-out shape; revisit only if the parameter list grows.
- Any change to interpolation behavior, map pruning policy, or `FrameInterpolate` itself.
- The game-`Frame.h`-in-PCH ordering itself (`Pch.h:96-97`) — stays as-is; this plan just stops an engine
  header from silently depending on it.

## Acceptance criteria
- `Graphics.h` no longer declares any by-value member of a game type; client builds clean with the member
  relocated; render behavior unchanged (pure state relocation, no logic edits).

## Notes
- Render-side state only — no determinism/CRC, replay, network, or `kiVersion` exposure. Client-only code
  paths (`GameBase::Render` / `Main.cpp` client loop).
- One grill decision: confirm `GameBase` as the destination (vs. documenting the status quo). The
  recommendation is the move — `GameBase` performs every per-frame mutation (the only other writer is the
  one-time boot population in `Main.cpp:216`).
- Guard detail for execution: the relocated member should sit in a `BT_CLIENT` span of `GameBase.h` (all
  readers/writers are client-only paths), matching the existing client-only sections there.

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source.
- `Graphics.h:103` member, `:3-8` `game::FrameInterpolate` forward decl, `:69` `RenderMainPresentAcquire`
  const-ref parameter — all exact. Graphics.cpp contains zero `mRenderInterpolates` references (only the
  parameter), confirming "stores state it never touches".
- Repo-wide `mRenderInterpolates` grep: `Graphics.h:103` (member), `GameBase.cpp:353,376,377` (prune/populate),
  `:397` (camera read), `:418` (pass-back), `Main.cpp:216` (boot populate), `:218` (boot camera read),
  `:226` (boot pass-back). No other code references. Critical-files list covers all of them.
- `Pch.h:96` (game `Frame/Frame.h`) before `Pch.h:97` (`Engine.h`) — confirmed; `GameBase.h:5-7` already
  includes the game `Graphics/Camera.h` under `BT_CLIENT`, so the proposed explicit include mirrors an
  existing pattern.
- Corrections made: Context previously said the map was populated/pruned "exclusively" by `GameBase::Render`
  — the boot path in `Main.cpp:212-218` also populates/reads it (now cited); known-reference list expanded
  with `GameBase.cpp:376,377` and `Main.cpp:216,218`.
- Benefit check: `GameBase` is the documented engine→game hub (`Engine/Source/CLAUDE.md` Hub Conventions);
  the move does not touch the sanctioned `game::gp*` read pattern. Incidental: relocating the map to
  `GameBase` also lets it survive the `DeviceLostException` Graphics re-construction (`Main.cpp:295-300`) —
  behavior-neutral (the next `Render` repopulates it today), no design change needed.

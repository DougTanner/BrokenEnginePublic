# Lighting Deposit Edge-Fade Inert On-Screen After Headroom Pre-Size

## Context

`LightingDepositEdgeFade` (`Engine/Data/Shaders/ShaderFunctions.h:257-262`) fades light deposits within the outer
5% of the **deposit texture** (it builds its UV as `f2FragCoord / (uiLightTiles{X,Y} * kiComputeTileSize)`, i.e. the
full deposit-texture dimensions, then `smoothstep(0, 0.05, edgeDistanceFromTextureEdge)`). Consumed by
`PointLight.frag` (~`:90`), `AreaLight.frag` (~`:90`), and `HexShieldLighting.frag` (~`:61`).

Before the `LightingDepositSpreadFullPort` change, the deposit texture covered ≈ the visible area, so this 5% band sat
at the **screen edge** and softened lights as they approached/left the frame. After that change the deposit texture is
pre-sized by the lighting headroom multiplier (`iRefMult = 3`, `TextureManager::LightingDetailTextureSize`) and covers
~3× the visible area, so the 5% band now sits ~0.85 visible-widths **off-screen**. Net effect: on-screen lights no
longer fade near the frame edge (the fade still prevents a hard cutoff at the real deposit-texture boundary, which is
now far off-screen).

This is arguably an *improvement* for the windowed pipeline (off-screen lights within the headroom legitimately
contribute via spread, and there is no on-screen pop as a light scrolls off the frame), but it is an unintended,
unconfirmed behavioral change to a shipped visual effect. Surfaced by the post-change affected-locations audit.

## Design

First decide intent (one grill question to the author): was the deposit edge-fade meant to soften lights at the
**screen edge**, or only to hide the hard cutoff at the **deposit-texture boundary**?

- **If the texture-boundary cutoff was the only purpose:** the current behavior is correct (the fade correctly tracks
  the real boundary, now off-screen, and the spread/combine/temporal window early-outs + 2-texel bilinear margin
  already prevent any on-screen hard edge). Resolution = confirm + document in the Lighting shader CLAUDE.md, no code
  change. Close the plan.
- **If a screen-edge vignette/softening was intended:** reframe the fade to the on-screen window — compute the
  fragment's visible-area UV (`WorldToVisibleArea(world, f4VisibleArea)`, the same basis the spread/combine/temporal
  windows already use) and `smoothstep` against the visible edge instead of the deposit-texture edge. The world
  position is reconstructible from `gl_FragCoord` via `f4LightingArea` (deposit vertex shaders already map through it),
  or pass the needed visible dims through the existing global uniform — no new per-frame plumbing beyond what
  `f4VisibleArea`/`f4LightingArea` already provide.

## Out of scope

- The windowing margins (spread remaining-reach, combine/temporal 2-texel) — those are correct and unrelated to this
  cosmetic fade.
- Any change to `f4LightingArea` sizing, the headroom multiplier, or the temporal pass.
- `LightingDepositEdgeFade`'s `0.05f` band width tuning (only the reference frame — texture-edge vs visible-edge — is
  in question).
- The visible-light sprite path (`VisibleLight.frag`) — it does not use this helper.

## Acceptance criteria

- Author intent recorded (texture-boundary-only vs screen-edge-vignette).
- If reframed: on-screen lights near the frame edge fade as before the headroom change, while off-screen-but-in-headroom
  lights still contribute through spread (verified by panning a bright light to the frame edge); no hard cutoff appears
  at the screen edge. If confirmed-as-intended: a one-line note in `Engine/Data/Shaders/Lighting/CLAUDE.md` explaining
  the fade now tracks the off-screen deposit boundary by design.

## Critical files

- `Engine/Data/Shaders/ShaderFunctions.h` (`LightingDepositEdgeFade`)
- `Engine/Data/Shaders/Lighting/PointLight.frag`, `AreaLight.frag`, `Objects/HexShieldLighting.frag` (consumers)
- `Engine/Data/Shaders/Lighting/CLAUDE.md` (documentation, either path)

## Notes

- Trivial-to-close if the conclusion is "texture-boundary-only" — most of the cost is the one intent question.
- No CRC/determinism/server involvement; client/graphics-only, shader-only edit if reframed (easily reverted).

# Plan: Resolve `fPbrSun` Cubic Multiply In Model.frag IBL Path

## Context

`Engine/Data/Shaders/Model/Model.frag` currently feeds `mainLayout.fPbrSun` into the IBL specular path *three times*. This is a long-standing pre-existing structural choice (predates the recent per-target Sun/Moon Intensity work) and has never been justified in writing. The relevant lines:

```glsl
// Sun/Moon color assignment (no per-target multiplier — that lives at the use sites):
vec3  f3SunColor      = mainLayout.fPbrSun * globalLayout.f4SunColor.rgb;
vec3  f3MoonColor     = mainLayout.fPbrSun * globalLayout.f4MoonColor.rgb;

// Luminance terms used only by the IBL specular path:
float fSunIntensity   = mainLayout.fPbrSun * (f3SunColor.r + f3SunColor.g + f3SunColor.b)
                        / mainLayout.fPbrDayBrightness;
float fMoonIntensity  = mainLayout.fPbrSun * (f3MoonColor.r + f3MoonColor.g + f3MoonColor.b)
                        / mainLayout.fPbrDayBrightness;

// BRDF consumer (line 300, linear in fPbrSun via f3SunColor):
color += max(globalLayout.fSunIntensityObjects * f3SunColor * fShadow * fShadow,
             globalLayout.fMoonIntensityObjects * f3MoonColor * fShadowMoon * fShadowMoon) * brdf;

// IBL consumer (line 309):
f3IblSpecular *= max(globalLayout.fSunIntensityObjects * fSunIntensity * f3SunColor * fShadow,
                     globalLayout.fMoonIntensityObjects * fMoonIntensity * f3MoonColor * fShadowMoon);
```

Algebra trace for the IBL specular path (sun side; moon symmetric):

```
fSunIntensity  = fPbrSun * sum(fPbrSun * f4SunColor.rgb) / fPbrDayBrightness
               = fPbrSun^2 * sum(f4SunColor.rgb) / fPbrDayBrightness

fSunIntensity * f3SunColor
               = fPbrSun^2 * sum(f4SunColor.rgb) / fPbrDayBrightness * fPbrSun * f4SunColor.rgb
               = fPbrSun^3 * f4SunColor.rgb * sum(f4SunColor.rgb) / fPbrDayBrightness
```

That is `fPbrSun` cubed inside the IBL contribution, while the direct BRDF term sees only a single `fPbrSun`. `fSunIntensityObjects` is now linear on both paths (the per-target multiplier sits at the use site, not baked into `f3SunColor`), so it does not contribute to the asymmetry.

There are two plausible interpretations:

1. **Intentional non-linear ramp.** The artist tuned `fPbrSun` knowing the IBL falloff curves cubicly, which makes the IBL highlight bloom much faster than the direct-lit term as the day brightens. Removing it would force re-tuning every PBR object's specular response.
2. **Bug / forgotten multiplier.** The luminance term `fSunIntensity` should weight the IBL by perceived brightness, not by `fPbrSun`-scaled brightness. The extra `fPbrSun` factor in the assignment line is a leftover from earlier iteration.

## Approach

Three-step plan, gated on the user's answer to step 1:

1. **Ask the user whether the cubic is intentional.** Show them this plan plus an annotated diff of the four lines with the algebra unrolled. Wait for explicit confirmation before editing.

2. **If intentional**: leave the math alone, but add a comment block above the assignment documenting the cubic intent so future audits don't re-flag it. No screenshot needed.

3. **If unintentional**: rewrite `fSunIntensity` (and the symmetric `fMoonIntensity`) to compute true Rec.709 luminance of the *unscaled* sun/moon color, leaving `fPbrSun` to apply linearly via `f3SunColor` / `f3MoonColor`:

   ```glsl
   const vec3 kRec709 = vec3(0.2126, 0.7152, 0.0722);
   float fSunLum  = dot(globalLayout.f4SunColor.rgb,  kRec709);
   float fMoonLum = dot(globalLayout.f4MoonColor.rgb, kRec709);
   float fSunIntensity  = fSunLum  / mainLayout.fPbrDayBrightness;
   float fMoonIntensity = fMoonLum / mainLayout.fPbrDayBrightness;
   ```

   IBL specular consumers then become `fSunIntensity * f3SunColor` = `fPbrSun * f4SunColor.rgb * fSunLum / fPbrDayBrightness` — linear in `fPbrSun`, matching the direct BRDF term's scaling behaviour.

## Files to Modify

- `Engine/Data/Shaders/Model/Model.frag` (the four assignment lines plus a comment block either way)
- Possibly `Engine/Data/Shaders/Model/CLAUDE.md` — document the chosen behaviour

## Risks / Open Questions

- **Visual regression risk is high.** Every PBR-shaded object on screen will shift if option 3 is taken. Capture before/after screenshots of: (a) a metallic object in direct sun, (b) the same in shadow with sky-only IBL, (c) night scene with moon dominant. Compare side-by-side before merging.
- **`fPbrSun` semantics**: confirm what `fPbrSun` actually represents — is it a per-material PBR strength (in which case a single linear application is clearly correct), or an exposure-like term? `mainLayout` is per-draw, so the answer governs whether per-material artist authoring needs re-tuning.
- **Out of scope for any in-flight per-target work**: this is a pre-existing structural choice, unrelated to the per-target Sun/Moon Intensity slider work. Address in a dedicated session.

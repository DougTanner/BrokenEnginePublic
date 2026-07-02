Feature: Lifetime color gradients for particles and curl noise for smoke spread
=================================================================================

Context
-------
Particles currently use a single packed color (iColor) set at spawn time. fIntensity fades over the particle lifetime via fIntensityDecay, but the color itself never changes. This limits visual richness -- fire particles can't transition from bright yellow to deep red to black, explosions can't shift hue as they cool.

Smoke spread uses Perlin-like noise (SmokeSpreadCommon.h) for displacement with swirl noise from sampling the noise texture at XY and YX coordinates. This produces smooth, repetitive motion. Curl noise (the analytical curl of a potential field) would add divergence-free turbulent swirls that look more physically plausible and less repetitive, matching the technique from Bridson 2007.

Why
---
- Color gradients give artists per-particle color transitions over lifetime (fire: white->yellow->orange->red->black, explosions: bright->dark)
- Curl noise produces divergence-free turbulent displacement that looks organic and never compresses/expands the smoke field unrealistically
- Both features are referenced in the Engine.txt notes (HDR bloom + gradient color, wobble smoke with swirl noise)

Changes (7 files)
-----------------

1. Engine/Data/Shaders/ShaderLayoutsBase.h
   Add two end-of-life color fields to ParticleLayout (after fRotationDeltaDecay, before f4Position):
     int32_t iColorEnd INIT;    // Packed RGBA end-of-life color (gradient target)
     float fSpawnTime INIT;     // globalLayout.fElapsedTime at spawn
     float fLifetime INIT;      // Total lifetime in seconds (0 = no gradient, use iColor only)
     float fParticlePad INIT;   // Padding to maintain 16-byte alignment

   Add curl noise tunables to GlobalLayout (after fSmokeEdgeDecayDistanceInv, before uiSmokeTilesX -- fSmokeNoiseInfluence was deleted by the DeadUboFieldsSweep):
     float fSmokeCurlNoiseScale INIT;     // World-space frequency of curl noise
     float fSmokeCurlNoiseStrength INIT;   // Displacement magnitude
     float fSmokeCurlNoiseSpeed INIT;      // Time animation speed
     float fSmokeCurlNoisePad INIT;        // Padding

2. Engine/Data/Shaders/Smoke/SmokeSpreadCommon.h
   Add a CurlNoise2D function before SmokeSpread that computes the 2D curl of a scalar noise potential field:
     vec2 CurlNoise2D(sampler2D noiseTextureSampler, vec2 f2Position, float fScale, float fTime)
     {
         float fEpsilon = 0.01f;
         vec2 f2ScaledPos = fScale * f2Position + vec2(fTime);
         float fN  = texture(noiseTextureSampler, f2ScaledPos).x;
         float fNx = texture(noiseTextureSampler, f2ScaledPos + vec2(fEpsilon, 0.0f)).x;
         float fNy = texture(noiseTextureSampler, f2ScaledPos + vec2(0.0f, fEpsilon)).x;
         float fDndx = (fNx - fN) / fEpsilon;
         float fDndy = (fNy - fN) / fEpsilon;
         return vec2(fDndy, -fDndx);
     }

   In SmokeSpread(), after computing f2Noise (line 21) and before the wind section (lines 23-41), add curl noise displacement:
     vec2 f2Curl = globalLayout.fSmokeCurlNoiseStrength * CurlNoise2D(noiseTextureSampler, f2WorldPosition, globalLayout.fSmokeCurlNoiseScale, globalLayout.fSmokeCurlNoiseSpeed * globalLayout.fElapsedTime);
     f2Noise += f2Curl;

3. Engine/Data/Shaders/Particles/ParticlesRender.frag
   Replace the single-color lookup with a gradient lerp based on lifetime fraction.
   After unpacking the color (line 45: `vec4 f4Color = unpackUnorm4x8(uiColor).abgr;` -- the SSBO color field is read into a local uiColor at line 39), add:
     vec4 f4ColorEnd = unpackUnorm4x8(render.pParticles[i].iColorEnd).abgr;
     float fLifetime = render.pParticles[i].fLifetime;
     float fAge = globalLayout.fElapsedTime - render.pParticles[i].fSpawnTime;
     float fLifetimeFraction = (fLifetime > 0.0f) ? clamp(fAge / fLifetime, 0.0f, 1.0f) : 0.0f;
     f4Color = mix(f4Color, f4ColorEnd, fLifetimeFraction);

   Note: the new iColorEnd/fLifetime/fSpawnTime reads should follow the same hoist-SSBO-reads-into-locals pattern used at lines 37-40 (read into locals once, then use the locals).

4. Engine/Source/Graphics/Managers/ParticleManager.cpp
   In ParticleManager::Spawn(), after setting layout.iCookie and before writing to the spawn buffer,
   stamp the spawn time from the global elapsed time:
     layout.fSpawnTime = gpBufferManager->GetElapsedTime();
   (Or pass elapsed time from the caller -- whichever pattern the engine uses for current time access in the spawn path.)

5. Engine/Source/Graphics/Render/SmokeUniforms.cpp
   After existing smoke tunable writes (around line 29), add curl noise uniforms:
     rGlobalLayout.fSmokeCurlNoiseScale = gSmokeCurlNoiseScale.Get();
     rGlobalLayout.fSmokeCurlNoiseStrength = gSmokeCurlNoiseStrength.Get();
     rGlobalLayout.fSmokeCurlNoiseSpeed = gSmokeCurlNoiseSpeed.Get();

6. Engine/Source/Ui/WrapperBase.h and WrapperBase.cpp
   Declare and define three new Wrapper tunables for curl noise:
     In .h (after smoke wrappers section):
       extern Wrapper gSmokeCurlNoiseScale;
       extern Wrapper gSmokeCurlNoiseStrength;
       extern Wrapper gSmokeCurlNoiseSpeed;
     In .cpp (after smoke wrappers section):
       Wrapper gSmokeCurlNoiseScale(0.02f, 0.001f, 0.1f);
       Wrapper gSmokeCurlNoiseStrength(0.003f, 0.0f, 0.02f);
       Wrapper gSmokeCurlNoiseSpeed(0.5f, 0.0f, 2.0f);

7. Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp
   Add the three curl noise sliders to the tweaks slider map (after existing smoke entries):
     {"Smoke Curl Noise Scale", &gSmokeCurlNoiseScale},
     {"Smoke Curl Noise Strength", &gSmokeCurlNoiseStrength},
     {"Smoke Curl Noise Speed", &gSmokeCurlNoiseSpeed},

Notes
-----
- Particles with fLifetime == 0 behave identically to before (no gradient, fLifetimeFraction stays 0)
- iColorEnd defaults to 0 (zero-initialized via INIT), so existing spawn code that doesn't set it will produce a fade-to-black gradient if fLifetime is set -- callers must set both iColorEnd and fLifetime together
- The curl noise function reuses the existing noise texture sampler already bound to the spread shaders, no new textures needed
- Curl noise is divergence-free by construction (curl of a scalar field), so it displaces smoke without creating artificial density compression or expansion
- ParticleLayout size increases by 16 bytes (4 new fields). This affects the spawn and particle storage buffers. Verify kiMaxParticles * sizeof(ParticleLayout) still fits within storage buffer limits
- fSpawnTime must be set on the CPU side during Spawn() so each particle records when it was born. The fragment shader computes age as (fElapsedTime - fSpawnTime)
- The existing fIntensity/fIntensityDecay system continues to work independently of the color gradient -- they stack (intensity fades the gradient-interpolated color)

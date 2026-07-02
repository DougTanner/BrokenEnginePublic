Destruction Buffer: Visual Surface Peeling on Damaged Entities
=============================================================

Context
-------
Models render via Cook-Torrance PBR shaders (Model.frag) with
per-material textures. Per-instance data is written into a storage
buffer of ModelLayout structs, which the vertex shader indexes via
gl_InstanceIndex. The fragment shader outputs final color after
combining BRDF, IBL, directional lighting, emissive, and smoke.

Currently there is no visual indication of progressive damage on
entities. Spaceships and players look pristine until they explode.
The only damage feedback is f4ColorAdd (a brief red/white freeze
flash on hit) which fades instantly.

Spaceships have pfHealths in SpaceshipsPostRender (deterministic,
CRC-validated). Players have pfArmors and pfShields in
PlayersPostRender. The Interpolate phase reads from the previous
PostRender to produce render-visible data. The render phase fills
ModelLayout storage buffers from Interpolate collections.

Reference: https://youtu.be/0x_lIq3FEQE?t=2011

Why
---
- Entities taking damage should visually degrade, giving players
  clear feedback about how much punishment a target has absorbed
- A damage fraction (0 = pristine, 1 = destroyed) computed from
  health/maxHealth is deterministic and requires no new network
  data since health already exists in the CRC-validated frame
- The effect uses a procedural noise threshold in the fragment
  shader to discard or darken fragments, creating an organic
  peeling/burn-away look with no additional textures
- The approach extends the existing ModelLayout struct with one
  float, keeping the per-instance GPU data compact
- Visual damage rendering is client-only; the server computes the
  same damage fraction but never renders it

Changes (8 files)
-----------------

1. Engine/Data/Shaders/ShaderLayoutsBase.h
   Add a damage fraction field to ModelLayout after f4ColorAdd:

     struct ModelLayout
     {
         vec4 f4Position INIT;
         vec4 f3x4Transform[3] INIT;
         vec4 f3x4TransformNormal[3] INIT;
         vec4 f4ColorAdd INIT;
         float fDamageFraction INIT; // 0 = pristine, 1 = fully destroyed
         uint32_t uiMeshDataBase INIT;
         uint32_t uiMaterialCount INIT;
     };

   DECISION: ShaderLayoutsBase.h has TWO model structs -- ModelLayout
   (~line 676) and ModelCustomLayout (~line 686, used by custom /
   hex-shield meshes, which also has f4ColorAdd). This plan only adds
   fDamageFraction to ModelLayout; explicitly decide at execution
   whether ModelCustomLayout is extended too or scoped out.

   Add destruction visual parameters to MainLayout after the hex
   shield section:

     // Destruction
     float fDestructionEdgeWidth INIT;    // Width of glowing edge band (world units)
     float fDestructionEdgeIntensity INIT; // Brightness of the edge glow
     float fDestructionNoiseScale INIT;   // UV scale for procedural noise
     float fDestructionDarkenPower INIT;  // Power curve for darkening near damage edge

2. Engine/Data/Shaders/Model/ModelCommon.h
   Pass fDamageFraction through the vertex stage. Add a new vertex
   output after f4OutColorAdd:

     layout (location = 8) out float fOutDamageFraction;

   In ModelVertexOutput(), write the value:

     fOutDamageFraction = model.fDamageFraction;

3. Engine/Data/Shaders/Model/Model.frag
   Add matching fragment input:

     layout (location = 8) in float fInDamageFraction;

   Add an #define ENABLE_DESTRUCTION 1 toggle near the other toggles.

   Add a procedural 3D hash function (no texture needed) before main():

     float Hash3D(vec3 p)
     {
         p = fract(p * vec3(443.897, 441.423, 437.195));
         p += dot(p, p.yzx + 19.19);
         return fract((p.x + p.y) * p.z);
     }

   After the smoke section and before the final f4OutColor write,
   add destruction logic gated by ENABLE_DESTRUCTION:

     #if ENABLE_DESTRUCTION
     if (fInDamageFraction > 0.0)
     {
         // Procedural noise threshold based on world position
         float fNoise = Hash3D(f3InWorldPosition * mainLayout.fDestructionNoiseScale);

         // Discard fragments where noise is below damage threshold (peeling away)
         float fThreshold = fInDamageFraction;
         if (fNoise < fThreshold)
         {
             discard;
         }

         // Darken and add glowing edge near the peel boundary
         float fEdgeDistance = fNoise - fThreshold;
         float fEdgeWidth = mainLayout.fDestructionEdgeWidth;
         if (fEdgeDistance < fEdgeWidth)
         {
             float fEdgeFactor = 1.0 - (fEdgeDistance / fEdgeWidth);
             // Darken the surface approaching the edge
             color *= mix(1.0, 0.2, pow(fEdgeFactor, mainLayout.fDestructionDarkenPower));
             // Add hot ember glow at the edge
             vec3 f3EmberColor = mix(vec3(1.0, 0.3, 0.0), vec3(1.0, 0.9, 0.3), fEdgeFactor);
             color += f3EmberColor * fEdgeFactor * mainLayout.fDestructionEdgeIntensity;
         }
     }
     #endif

4. Engine/Data/Shaders/Model/ModelShadow.frag
   If ModelShadow.frag uses the same vertex outputs, add the
   fInDamageFraction input and discard logic (without the edge
   glow) so shadow maps also show holes in damaged geometry:

     if (fInDamageFraction > 0.0)
     {
         float fNoise = Hash3D(f3InWorldPosition * mainLayout.fDestructionNoiseScale);
         if (fNoise < fInDamageFraction)
         {
             discard;
         }
     }

   If ModelShadow.frag does not have world position or the main
   layout binding, skip this file and accept that shadows remain
   solid (low visual impact).

5. Projects/BrokenEngineSandbox/Source/Frame/Collections/
   Spaceships/SpaceshipsRender.cpp
   In the processRange lambda, after setting f4ColorAdd, compute
   and write fDamageFraction from the previous frame's health:

     float fMaxHealth = kfSpaceshipHealth;
     float fHealth = rPreviousPostRender.pfHealths[i];
     rModelLayout.fDamageFraction = std::clamp(1.0f - (fHealth / fMaxHealth), 0.0f, 1.0f);

   This requires passing rPreviousPostRender into the lambda.
   In SpaceshipsInterpolate::Render, capture the previous
   PostRender from the FrameInterpolate (which already provides
   access to the previous frame's PostRender data for
   interpolation). Alternatively, add a client-only
   pfDamageFractions array to SpaceshipsInterpolate, computed
   during SpaceshipsInterpolate::Update from the previous
   PostRender health, keeping the render function a pure layout
   writer.

   Preferred approach: Add pfDamageFractions to
   SpaceshipsInterpolate as a client-only member (in
   ClientMembers), compute it in Update, and read it in Render.

6. Projects/BrokenEngineSandbox/Source/Frame/Collections/
   Spaceships/Spaceships.h
   Add to SpaceshipsInterpolate, inside the #if defined(BT_CLIENT)
   block alongside pfAnimationTimes:

     float* __restrict pfDamageFractions = nullptr;

   Add it to ClientMembers() tie.

7. Projects/BrokenEngineSandbox/Source/Frame/Collections/
   Players/PlayersRender.cpp
   In the render loop, after setting f4ColorAdd, compute
   fDamageFraction from armor:

     float fMaxArmor = kfPlayerArmor;
     rPlayerLayout.fDamageFraction = std::clamp(1.0f - (rCurrent.pfArmors[i] / fMaxArmor), 0.0f, 1.0f);

   PlayersPostRender fields are not directly available in Render.
   Same approach as spaceships: add a client-only
   pfDamageFractions to PlayersInterpolate, compute in
   PlayersInterpolate::Update from previous PostRender armor,
   read in Render.

8. Projects/BrokenEngineSandbox/Source/Frame/Collections/
   Players/Players.h
   Add to PlayersInterpolate, inside the #if defined(BT_CLIENT)
   block:

     float* __restrict pfDamageFractions = nullptr;

   Add it to ClientMembers() tie.

Notes
-----
- The game-side SOA member names referenced above (pfHealths,
  kfSpaceshipHealth, pfArmors, ClientMembers()) must be verified
  against the current game code at execution time; the new
  pfDamageFractions members require the add-collection-member
  checklist
- The Hash3D function produces deterministic noise from world
  position, so the peeling pattern is stable as the camera moves
  and consistent across frames (no shimmer)
- fDamageFraction is derived from health which is already
  CRC-validated; the visual is fully deterministic across
  client/server (though only rendered on client)
- The discard approach means damaged areas become transparent,
  showing whatever is behind (sky, terrain). For enclosed models
  this naturally reveals interior geometry if it exists
- MainLayout destruction parameters should be wired to the
  existing settings/tuning UI so artists can adjust edge width,
  glow intensity, noise scale, and darken power at runtime
- Players use armor (not shield) for damage fraction because
  shield regenerates and visual peeling should reflect permanent
  hull damage
- The procedural noise scale should be tuned relative to model
  size; spaceships and players may want different scales, which
  could be done via push constants or the ModelLayout struct if
  per-entity control is needed later
- Performance impact is minimal: one hash + branch per fragment
  for damaged entities, zero cost for undamaged (early out on
  fDamageFraction == 0)

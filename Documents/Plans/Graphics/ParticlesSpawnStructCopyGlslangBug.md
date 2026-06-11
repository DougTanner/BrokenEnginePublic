# Particles Spawn Whole-Struct SSBO Copy vs glslang Trailing-Field-Drop Bug

## Context

`Engine/Data/Shaders/Particles/ParticlesSpawn.comp` (the slot-fill, line ~88) does a **whole-struct
SSBO-to-SSBO** copy:

```glsl
particles.pParticles[i] = spawn.pParticles[iSpawnIndex++];
```

This copies a full `Particle` struct between two scalar-block-layout SSBOs. The repo documents a glslang compiler
bug at `Engine/Data/Shaders/Terrain/Terrain.vert:70-72`: "glslang's struct-copy from a scalar-block-layout SSBO
has been observed to drop trailing fields on some drivers (rotation read as 0); reading each field through
`pQuads[...]` avoids it." Both `Terrain.vert` and the Quads/Particles update+render shaders deliberately read
SSBO struct fields **one at a time** to dodge this bug.

The Particles spawn copy is the **one whole-struct copy** in the particle pipeline. `Particles/CLAUDE.md`
explicitly flags it as the documented exception and asserts it is safe: "Update and render shaders read SSBO
struct fields one at a time, never whole-struct copies … spawn's slot fill is the one whole-struct copy
(SSBO-to-SSBO, **not into a local**)." The stated theory: the glslang bug manifests when copying *into a local*
struct (the read side), and a pure buffer-to-buffer store may not be affected.

This plan **verifies that the buffer-to-buffer exception is actually safe** (rather than a latent trailing-field
drop that silently zeroes the last `Particle` field on spawn — which under a top-down VFX system could read as
"newly spawned particles occasionally start with a zeroed last field" and be easy to miss). The deliverable is
either a confirmation (close + keep the documented rationale, optionally strengthen the comment) or a fix (split
the copy into per-field stores like the update/render path).

## Design

1. **Identify the `Particle` struct's trailing field** in `ShaderLayoutsBase.h` (the field most at risk if the
   bug applies to stores). Establish whether a zeroed trailing field on spawn would be observable (does any
   consumer — `ParticlesUpdate.comp` / the render verts — read it on the first frame, or is it overwritten
   before first use?).
2. **Determine whether the glslang bug applies to SSBO→SSBO stores or only SSBO→local reads.** The existing
   `Terrain.vert` comment describes the symptom on the *read into a local*; confirm whether the same codegen
   path is exercised by a direct `bufferA[i] = bufferB[j]` store (glslang may lower it through a temporary,
   re-exposing the bug, or as a direct copy that is safe). If the toolchain can be checked (decompile the SPIR-V
   for this shader and inspect whether all members are copied), that is the definitive check; otherwise reason
   from the documented symptom + the SDK glslang version.
3. **If safe (confirmed):** close — keep the per-field discipline elsewhere and the `Particles/CLAUDE.md` note;
   optionally tighten the in-shader comment at the copy site to state *why* this one whole-struct copy is
   permitted (buffer-to-buffer, no local) so a future editor doesn't "fix" it into a field-by-field copy
   needlessly or, worse, copy the pattern into a read-into-local site.
4. **If latent bug (confirmed or unresolved-but-plausible):** replace the whole-struct copy with explicit
   per-field stores (mirror the update/render field-at-a-time pattern), so the trailing field is written
   unconditionally. Cheap, removes the risk, costs one SSBO struct copy's worth of source verbosity.

## Out of scope

- **The update/render shaders** — already read field-at-a-time; unchanged.
- **The `Particle` struct layout** in `ShaderLayoutsBase.h` — not reordered/resized; this is about the copy, not
  the data.
- **The `Terrain.vert` / Quads field-by-field reads** — those are the established mitigation and stay; this plan
  only addresses the spawn store.
- **Determinism** — particles are client-side VFX only (`Particles/CLAUDE.md`: "not deterministic sim state"), so
  there is no CRC/replay exposure even if the field is wrong.

## Acceptance criteria

- A determination on record of whether the glslang trailing-field-drop bug affects the SSBO→SSBO store at the
  spawn slot-fill (with the evidence: SPIR-V inspection or documented-symptom reasoning + the at-risk trailing
  field identified).
- If safe: the in-shader comment states why this whole-struct copy is the sanctioned exception; no code change.
- If a bug: the copy is split into per-field stores, the affected shaders recompile via DataPacker, and particles
  still spawn/render correctly (visual smoke-test — not compile-checked).

## Critical files

- `Engine/Data/Shaders/Particles/ParticlesSpawn.comp` (the `particles.pParticles[i] = spawn.pParticles[...]`
  slot-fill copy, ~line 88).
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — the `Particle` struct definition (trailing-field identification).
- Read-only reference: `Engine/Data/Shaders/Terrain/Terrain.vert` (`:70-72`, the documented bug + mitigation),
  `Engine/Data/Shaders/Particles/CLAUDE.md` (the "one whole-struct copy, SSBO-to-SSBO not into a local"
  rationale), `Engine/Data/Shaders/Particles/ParticlesUpdate.comp` (the field-at-a-time read pattern).

## Notes

- **Investigation-first** (Diagnosis Discipline) — could be a no-op confirmation or a small per-field-store fix;
  do not split the copy without first establishing the bug applies to stores, since the field-by-field form is
  more verbose and the doc already claims the copy is safe.
- Shader-only; client/graphics-only; **no determinism/CRC exposure** (particles are non-deterministic VFX).
- If a fix is taken it requires a DataPacker recompile + a visual smoke-test, not a C++ build.
- Candidate for the `/glsl-review` skill's scalar-block-layout / struct-copy check at execution.

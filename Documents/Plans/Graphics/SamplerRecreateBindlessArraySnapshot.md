# Fix Sampler-Recreate Snapshot Clobber For Bindless Island Arrays

## Context

`PipelineDescriptorWriter::Write` registers a CRC-0 `TextureBinding` entry for
every bindless-array DescriptorInfo (the "Register under CRC 0 for sampler
recreation coverage" line in
`Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp:~476`). The entry
snapshots `ppTextures` into a `std::vector<Texture*>` via
`RegisterTextureBinding` at
`Engine/Source/Graphics/Managers/TextureDescriptors.cpp:209` (`textures.assign(ppTextures, ppTextures + iCount)`).

At pipeline-create time, every slot in `mElevationTextures` /
`mColorTextures` / `mNormalsTextures` / `mAmbientOcclusionTextures` /
`mMasksTextures` still points at the slot-0 placeholder (set up by the fan-out
loop in `TextureManager.cpp:261-268`). So the CRC-0 `TextureBinding.textures`
vector holds 16 placeholder pointers at boot.

`AcquireTextureSlot` later overwrites the live array slots in place
(`mElevationTextures.at(iSlot) = &rTemplate.mElevationTexture` etc. at
`IslandTerrain.cpp:296-300`). The CRC-0 snapshot is not refreshed.

If samplers ever get recreated, `RewriteSamplerDescriptors` at
`TextureDescriptors.cpp:278-313` iterates `mTextureBindings`, hits the CRC-0
entry, and calls `WriteArrayBindingDescriptors` with `iArrayIndex < 0` — which
writes the *full array* using the stale snapshot (placeholder pointers) at
`TextureDescriptors.cpp:169-194`. Result: every island-keyed slot's descriptor
gets clobbered with the placeholder view, and the live per-slot bindings that
`AcquireTextureSlot` registered are lost.

This is **pre-existing behavior** — the CRC-0 snapshot pattern predates the
`kBindlessArrayConsumer` refactor. The
`CoLocatePerSlotDescriptorRegistration.md` plan deliberately scoped this out
to keep the diff focused on collapsing the split-across-files registration
list.

## Design

Pick one of:

- **Option A — Skip CRC-0 push for flagged entries; drive sampler recreation
  off `mBindlessArrayConsumers`.** In `RewriteSamplerDescriptors`, after
  iterating `mTextureBindings`, iterate `mBindlessArrayConsumers` and for
  each consumer re-run the per-slot writes using the *live* array pointer
  (`ppArray = consumer's map key`) and each slot's current `Texture*`. Avoids
  the snapshot entirely.

- **Option B — Change the CRC-0 TextureBinding to store the live array
  pointer (`Texture**`), not a copy.** Replace
  `std::vector<Texture*> textures` (for the CRC-0 + bindless case) with a
  borrowed `Texture**` + `iCount`. `WriteArrayBindingDescriptors`' full-array
  branch reads through the live pointer. Smaller change, but introduces a
  storage variant on `TextureBinding`.

- **Option C — Repopulate the CRC-0 snapshot at each
  `AcquireTextureSlot` first-mint.** Each time a slot becomes live, walk
  `mTextureBindings[0]` and overwrite the matching `textures[iSlot]` pointer
  for every consumer of that array. Most invasive at the call site; least
  invasive elsewhere.

Option A is cleanest — eliminates the snapshot footgun by construction.

## Acceptance criteria

- Trigger a sampler recreate after at least one island has first-minted. The
  live per-slot view bindings must survive the recreate (shadow elevation
  must still render correctly for that island).
- No CRC-0 snapshot of placeholder views for any `kBindlessArrayConsumer`
  DescriptorInfo (or, if Option B/C, the snapshot must be live at recreate
  time).

## Out of scope

- The `kBindlessArrayConsumer` flag itself and the
  `mBindlessArrayConsumers` registry — already landed.
- Non-bindless-array DescriptorInfos — their CRC-0 entries are still useful
  for the single-texture-pointer case (`pTexture != nullptr`) which has no
  live mutation problem.

## Critical files

- `Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp` — the CRC-0
  registration line (`~line 476`) is the source of the stale snapshot.
- `Engine/Source/Graphics/Managers/TextureDescriptors.cpp` —
  `RewriteSamplerDescriptors` (lines 268-313) and `WriteArrayBindingDescriptors`
  (lines 135-194) are where the clobber would manifest.
- `Engine/Source/Graphics/Managers/TextureDescriptors.h` — `TextureBinding`
  struct definition.

## Notes

- Surfaced by the audit step of the `CoLocatePerSlotDescriptorRegistration.md`
  execution session. Pre-existing — not introduced by that refactor.
- Sampler recreation is rare (settings change such as anisotropy toggle), so
  the bug class has presumably been latent. Reproducing requires changing a
  sampler setting after islands have minted.

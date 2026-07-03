# Data-Texture Sampler Mip-LOD Bias

## Context

The global texture sharpen slider `gMipLodBias` is applied as `.mipLodBias = -gMipLodBias.Get()` on the base sampler create-info in `TextureManager::CreateSamplers` (`TextureManager.cpp`, the `vkSamplerCreateInfo` at the `mipLodBias = -gMipLodBias.Get()` line), so every sampler minted from it — `kSamplerSlotClamp`, `kSamplerSlotElevation`, `kSamplerSlotBorder`, `kSamplerSlotBorderWhite`, `kSamplerSlotRepeat`, `kSamplerSlotMirroredRepeat` — carries a negative (sharpen) mip bias. A sharpen bias tuned for **albedo/color** pushes **non-color data-texture** fetches (normal maps, metallic-roughness, control/blend masks) toward noisier, less-averaged mips, which aliases the data those maps drive (specular normals, blend boundaries).

The water-specular session already fixed the water-normal case: it split the water-normal atlas onto its own `kSamplerSlotMirroredRepeatWater` sampler with a dedicated slider-driven bias `gWaterNormalMipBias` (default 0, applied directly, not negated) instead of inheriting the global sharpen, because a sharpen bias on the water normals both shimmered the specular and desynced `Water.frag`'s `WATER_SPEC_AA_MIP_HANDOFF` analytic LOD from the hardware LOD. That fix is the precedent this plan generalizes to the other data-texture sites.

A sweep found the same "albedo sharpen bias applied to non-color data textures" pattern at these unaddressed sites (all citations verified against current source):

- **`Model.frag` normal map** (`GetNormal`, the BC5 `pTextures[nonuniformEXT(...fNormalTextureIndex)]` fetch at `Model.frag:184`, feeds GGX specular via the perturbed `n`) and **metallic-roughness map** (`pTextures[...fPhysicalDescriptorTextureIndex]` at `Model.frag:231`, drives `perceptualRoughness`/`metallic`). Both are fetched through the shared global Set-0 sampler `samplerRepeat` (`layout(set=0, binding=kiGlobalBindingSamplerRepeat)`), written from `mpSamplers[kSamplerSlotRepeat]` in `TextureDescriptors.cpp` (the `samplerRepeatInfo` write). That same `samplerRepeat` **also** serves the model's albedo (`Model.frag:223`), occlusion (`:269`), and emissive (`:395`) fetches on the same bindless `pTextures[]` array — so it **cannot** simply be zeroed; separating the data fetches needs a dedicated sampler. **Highest severity** (specular consumer). Interacts with `Graphics/ModelSpecularAntialiasing.md` — its `## Notes` zoom-residual item observes low-roughness models still flicker under camera zoom because the screen-space-derivative AA can't see variance the mip chain already averaged away; a sharpen bias on the normal fetch makes that worse by selecting noisier mips.
- **`Terrain.frag` island BC5 normals** (`normalTextureSamplers` fetch at `Terrain.frag:69`) — bindless array bound with `kSamplerClamp` at `PipelineManager.cpp:399`. **Diffuse-only consumer** (N·L directional lighting, no specular), lower severity.
- **`Terrain.frag` sand/rock detail normals** (`sandNormalsSampler0/1/2` at `Terrain.frag:97`, `rockNormalsSampler0/1/2` at `Terrain.frag:88`, via `SampleNormal`) — bound `kSamplerRepeat` at `PipelineManager.cpp:403-405` (sand) and `:407-409` (rock). Diffuse-only, lower severity. Their matching diffuse rock/sand **color** maps (`rockSampler` `:402`, `sandSampler` `:406`) are separate `kSamplerRepeat` bindings and legitimately want the sharpen.
- **Per-island material masks BC7 control map** (`masksTextureSamplers` at `Terrain.frag:81`) — bound `kSamplerClamp` at `PipelineManager.cpp:419`; and **island AO** (`ambientOcclusionTextureSamplers` at `Terrain.frag:136`) — `kSamplerClamp` at `PipelineManager.cpp:400`. Blend-weight / data maps; the sharpen aliases material boundaries at minification.
- **`Water.frag` noise** (BC4, `kSamplerRepeat` at `PipelineManager.cpp:466`) uses explicit `textureGrad` with hand-computed derivatives, so the sampler mip-bias is largely bypassed; **depthLut** (`PipelineManager.cpp:468`, default `kSamplerClamp`) is a single-row LUT sampled at `vec2(x, 0)` and is effectively single-mip. Both listed for completeness — **probably accept**.

Note: the terrain sites (bound per-binding through `PipelineDescriptorInfo::flags` in `PipelineManager.cpp`) can each take a dedicated sampler flag independently without touching their sibling color bindings. The Model sites are different: `samplerRepeat` is a **global Set-0 sampler** (binding 3), so splitting the Model data fetches requires a **new global Set-0 sampler binding** (binding **2** is documented "intentionally unused" in `ShaderLayoutsBase.h`, available) plus a new `Model.frag` sampler declaration.

**Invariant exposure: client/graphics-only.** Sampler create-info + descriptor-flag + Set-0 descriptor-write work, plus (if Option A is taken) shader edits selecting a different sampler for the affected fetches. Any shader edit requires a DataPacker **shader repack**. **No** `.pack`/CRC/`kiVersion`/wire/determinism/replay exposure; no sim-frame or allocation-tracked-main-loop paths. If a slider-driven bias (rather than a fixed 0) is chosen for the new sampler, it adds a `MainLayout` uniform field + Ui wrapper exactly like the existing `gWaterNormalMipBias` / `fWaterNormalMipBias` pair (per-frame CPU-populated, not serialized/CRC'd) — same non-exposure class as every `fPbr*`/`fWaterSpecAA*` field.

This is a **decision plan**: the deliverable is the decision (per site) plus, at most, the Model data-sampler split. It is survey-sized; do not implement all sites speculatively.

## Design

Two options, decidable per site (they are not mutually exclusive across sites).

### Option A — dedicated no-bias (or slider-driven) sampler for data-texture fetches

Give the aliasing data fetches a sampler that does **not** carry the albedo sharpen — either a fixed `mipLodBias = 0.0`, or a dedicated slider (`gMipLodBiasData`-style), directly applied like the water-normal precedent. Priority order:

1. **Model normal + metallic-roughness (highest value — specular + zoom-residual interaction).**
   - Add a sampler slot (e.g. `kSamplerSlotRepeatData`) in `TextureManager::CreateSamplers`, cloned from the `kSamplerSlotRepeat` create-info but with `mipLodBias = 0.0` (or a `std::clamp`ed slider value, mirroring the `kSamplerSlotMirroredRepeatWater` block). Add the matching `DescriptorFlags::kSamplerRepeatData` → slot mapping in `GetSampler`'s `kFlagToSlot` table.
   - Add a global Set-0 binding constant in `ShaderLayoutsBase.h` (reuse the free **binding 2**), e.g. `kiGlobalBindingSamplerRepeatData`; add the `VK_DESCRIPTOR_TYPE_SAMPLER` layout entry + descriptor write in `TextureDescriptors.cpp` (a `samplerRepeatDataInfo` write alongside `samplerRepeatInfo`/`samplerClampInfo`, using `mpSamplers[kSamplerSlotRepeatData]`).
   - In `Model.frag`, declare `layout(set=0, binding=kiGlobalBindingSamplerRepeatData) uniform sampler samplerRepeatData;` and switch **only** the normal-map fetch (`:184`) and the metallic-roughness fetch (`:231`) to it. Leave albedo/occlusion/emissive on `samplerRepeat`.
2. **Terrain normals (island BC5 + sand/rock detail) — second priority, diffuse-only.** For the `PipelineManager.cpp` per-binding sites this is cheaper: introduce dedicated no-bias sampler flag(s) and change only the normal-map binding flags — island normals `:399` (`kSamplerClamp`→`kSamplerClampData`), sand normals `:403-405` and rock normals `:407-409` (`kSamplerRepeat`→`kSamplerRepeatData`). No Set-0 binding or shader edit needed (the samplers are combined-image-samplers baked into the descriptor per binding). Leave the sibling color/diffuse bindings unchanged.
3. **Masks + AO — optional, blend-weight maps.** Same per-binding flag swap on `PipelineManager.cpp:419` (masks) and `:400` (AO) if A/B shows boundary aliasing worth fixing.

### Option B — accept + document

The bias is user-tunable and defaults to a sharpen that users generally want; a visual A/B may show no meaningful harm on the diffuse-only consumers (terrain normals/AO/masks) at RTS zoom. If so, accept and document the shared-sampler coupling at each site (a comment at `CreateSamplers` and at the relevant `PipelineManager.cpp` binding lines noting the data textures deliberately inherit the albedo sharpen), and record why the Model case is or isn't worth the dedicated-sampler machinery.

### Recommended framing for the grill

- **Model normal/MR:** lean Option A (specular consumer; compounds the `ModelSpecularAntialiasing` zoom residual). Decide fixed-0 vs slider bias — fixed 0 is the minimal change (no uniform/Ui wiring); a slider mirrors `gWaterNormalMipBias` and lets the analytic-AA tuning track it, at the cost of a `MainLayout` field.
- **Terrain normals:** lean Option A only if A/B shows visible shimmer; otherwise B (diffuse-only, N·L integrates out much of the noise).
- **Masks/AO, Water noise, depthLut:** lean Option B (accept + document); water noise uses `textureGrad`, depthLut is single-mip.

## Critical files

- **`Engine/Source/Graphics/Managers/TextureManager.cpp`** — `CreateSamplers`: the base `vkSamplerCreateInfo` with `mipLodBias = -gMipLodBias.Get()` (root of the shared bias); add any new data sampler slot(s) here, mirroring the `kSamplerSlotMirroredRepeatWater` block. `DestroySamplers` iterates `mpSamplers` (no change needed if the slot count grows via the enum).
- **`Engine/Source/Graphics/Managers/TextureManager.h`** — `SamplerSlot` enum + `mpSamplers` array size; `GetSampler` / `DescriptorFlags` sampler-flag additions.
- **`Engine/Source/Graphics/Managers/TextureDescriptors.cpp`** — global Set-0 sampler layout entries + the per-set descriptor writes (the `samplerRepeatInfo`/`samplerClampInfo` block, ~`:99-108`); add a `samplerRepeatDataInfo` write if the Model split is taken.
- **`Engine/Source/Graphics/Managers/PipelineManager.cpp`** — terrain per-binding sampler flags: island normals `:399`, sand normals `:403-405`, rock normals `:407-409`, masks `:419`, AO `:400` (change only the ones the decision selects).
- **`Engine/Data/Shaders/ShaderLayoutsBase.h`** — global Set-0 binding constants (`kiGlobalBindingSamplerRepeat = 3`, `kiGlobalBindingSamplerClamp = 12`, free binding 2); add a data-sampler binding constant if the Model split is taken.
- **`Engine/Data/Shaders/Model/Model.frag`** — new `samplerRepeatData` declaration; switch the normal fetch (`:184`) and metallic-roughness fetch (`:231`) to it (leave albedo/occlusion/emissive on `samplerRepeat`).
- **`Engine/Data/Shaders/Terrain/Terrain.frag`** — no source change under Option A for terrain (the sampler swap is entirely in `PipelineManager.cpp` binding flags); listed only as the consumer reference for the affected fetches (`:69`, `:88`, `:97`, `:81`, `:136`).
- **(If a slider bias is chosen)** the `PbrWrappersBase.*` / a mip-bias wrapper + `TweaksScreen*` slider + `LightingUniforms.cpp` `MainLayout` copy — same wiring shape as `gWaterNormalMipBias`. Prefer fixed 0 to avoid this unless the grill wants tunability.
- **`Engine/Data/Shaders/Model/CLAUDE.md`** and **`Engine/Data/Shaders/Terrain/CLAUDE.md`** — a one-line note on the data-vs-color sampler split (whichever sites land), mirroring the Water CLAUDE.md `kSamplerMirroredRepeatWater` bullet.

## Out of scope

- **The water-normal sampler** — already split onto `kSamplerSlotMirroredRepeatWater` with `gWaterNormalMipBias`; this plan does not touch it or `Water.frag`'s AA path.
- **Water noise / depthLut** — `textureGrad` / single-mip; accept (documented, not re-sampled).
- **`ModelSpecularAntialiasing.md`'s analytic AA / `kSampleShading` drop** — that plan owns the GGX geometric-AA kernel and the sample-shading investigation. This plan only removes the sharpen bias from the Model normal/MR fetches; the two are additive (both reduce specular flicker) and share `Model.frag` + `ShaderLayoutsBase.h` (line-drift only). If both are wanted, land `ModelSpecularAntialiasing` first or refresh cites.
- **Changing the global `gMipLodBias` default or semantics** — the albedo sharpen stays; this plan only stops it aliasing the data textures.
- **Anisotropy / filter-mode changes on any sampler** — only the mip-LOD bias is in scope.
- **DataPacker mip-generation or per-mip variance bake** — no pack-format change; the `TextureHeader::pfMipVariance` bake (used by the water AA) is unrelated.
- **Implementing every listed site speculatively** — the deliverable is the decision plus, at most, the Model data-sampler split; terrain/masks/AO land only if A/B justifies.

## Notes

- **Decision plan (present options).** Grill decisions: (1) per-site Option A (dedicated sampler) vs Option B (accept + document) — recommend A for Model normal/MR, B-unless-A/B-justifies for terrain/masks/AO, B for water noise/depthLut; (2) if A for Model: fixed `mipLodBias = 0.0` (minimal) vs a dedicated slider mirroring `gWaterNormalMipBias` (tunable, adds a `MainLayout` field + Ui wiring).
- **Precedent to mirror:** the landed water-normal split — `kSamplerSlotMirroredRepeatWater` + `gWaterNormalMipBias` (default 0, applied directly not negated) in `CreateSamplers`, bound at `PipelineManager.cpp:467`. Same shape for any new data sampler.
- **Free descriptor binding:** Set-0 binding **2** is documented "intentionally unused" in `ShaderLayoutsBase.h` — reuse it for a global data sampler rather than renumbering.
- **Invariant exposure declared:** client/graphics-only; sampler + descriptor-flag work; shader repack required for any `Model.frag` edit; no `.pack`/CRC/`kiVersion`/wire/determinism/replay exposure; a slider variant adds a per-frame `MainLayout` uniform (not serialized/CRC'd), same class as `fWaterNormalMipBias`.
- **Shared-file overlaps (see `Order.md` File Groups):** `TextureManager.cpp` (`CreateSamplers`), and the `PipelineManager.{h,cpp}` + `TextureDescriptors.{h,cpp}` pipeline cluster — refresh citations if co-scheduled; never interleave with `Graphics/Managers/Architecture_BindlessSlotLifecycle.md` (it reshapes `TextureDescriptors`). Line-drift only, no logic conflict.

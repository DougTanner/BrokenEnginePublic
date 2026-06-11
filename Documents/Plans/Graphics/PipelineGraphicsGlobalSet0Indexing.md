# Pipeline Graphics Set-0 Indexing vs Always-Framebuffer Doc Claim

## Context

`Engine/Source/Graphics/Objects/CLAUDE.md` states the bind-time set-indexing rule (Architecture Notes,
"Bind-time set indexing"): "**global Set 0 is always indexed by framebuffer**; the per-pipeline set uses the
framebuffer index only for per-command-buffer pipelines (host-visible indirect buffers force this mode), else
slot 0. Compute bind logic is exported as a free function (`BindComputeDescriptorSets`) so external record sites
apply the same rule."

The code in `Engine/Source/Graphics/Objects/Pipeline.cpp` does **not** uniformly honor "Set 0 always
framebuffer-indexed" on the graphics path:

- `BindComputeDescriptorSets` (the documented free function) indexes Set 0 with `iCommandBuffer` (the framebuffer
  index) **always** — matching the doc. Its inline comment: "Global Set 0 is indexed per-framebuffer
  (iCommandBuffer); per-pipeline Set 1 follows mbPerCommandBuffer (iDescriptorSetIndex)."
- `BindGraphicsDescriptorSets` indexes **both** Set 0 *and* Set 1 with a single `iDescriptorSetIndex` parameter
  (`mGlobalDescriptorSets[iDescriptorSetIndex]`, `rDescriptorSets[iDescriptorSetIndex]`).
- The plain-draw graphics record sites pass `iDescriptorSetIndex = mbPerCommandBuffer ? iCommandBuffer : 0`:
  - `Pipeline::RecordBindPipelineDescriptorsAndDraw` (the draw record) and `RecordBindPipelineAndDescriptors`
    both compute `iDescriptorSetIndex = mbPerCommandBuffer ? iCommandBuffer : 0` and pass it to
    `BindGraphicsDescriptorSets`. So for a **non-per-command-buffer plain graphics draw**, Set 0 binds
    `mGlobalDescriptorSets[0]` in *every* framebuffer's command buffer — not the per-framebuffer index.
  - `RecordDrawIndirect` instead passes `iCommandBuffer` directly to `BindGraphicsDescriptorSets` for Set 0 — so
    the indirect graphics path *does* framebuffer-index Set 0, while the plain-draw path does not. The graphics
    path is thus internally inconsistent.

So either: (a) the doc overstates — Set 0 is framebuffer-indexed for compute and indirect-graphics but slot-0 for
plain non-per-command-buffer graphics draws (and that is fine because those pipelines' Set-0 global descriptors
are identical across framebuffers); or (b) it is a latent bug — `mGlobalDescriptorSets[0]` bound into every
framebuffer's command buffer can reference resources the per-framebuffer Set-0 was meant to isolate (frames in
flight), risking the same class of cross-frame GPU conflict that "per-framebuffer descriptor sets prevent" (per
`Graphics/CLAUDE.md`).

This plan is an **investigation + reconcile**, not a blind code change: determine whether plain-draw Set 0 binding
`mGlobalDescriptorSets[0]` everywhere is correct (Set-0 globals are per-framebuffer-identical for those pipelines)
or a bug (some Set-0 content differs per framebuffer), then either correct the code to framebuffer-index Set 0 on
the graphics plain-draw path (mirroring compute/indirect) or correct the doc to describe the real per-path rule.

## Design

1. **Establish what Set 0 holds and whether it varies per framebuffer.** Set 0 = TextureManager global
   (UBOs, samplers, bindless arrays — `mGlobalDescriptorSets[]`). Determine whether `mGlobalDescriptorSets[i]`
   for different `i` differ in any way the GPU would observe within a frame in flight (per-framebuffer UBO
   contents, image-info that changes as slots evict). The eviction-symmetry/descriptor-patch machinery
   (`Graphics/CLAUDE.md` RenderGlobal descriptor-patch window, Managers/CLAUDE.md eviction invariant) is the
   relevant context — if Set-0 per-framebuffer copies diverge transiently during patching, binding `[0]` in all
   command buffers is wrong for plain draws.
2. **Enumerate plain-draw graphics pipelines that are `!mbPerCommandBuffer`** (those are the affected ones —
   per-command-buffer pipelines already pass `iCommandBuffer`). Confirm whether any of them have a Set-0 binding
   that legitimately needs the per-framebuffer copy.
3. **If correct-as-is:** the fix is doc-only — reword the Objects/CLAUDE.md "always indexed by framebuffer"
   claim to the real rule (Set 0 is framebuffer-indexed for compute and indirect-graphics; plain non-per-command-
   buffer graphics draws bind the slot-0 global because that pipeline's Set-0 globals are framebuffer-invariant).
   Note the graphics-path internal inconsistency (plain-draw uses `iDescriptorSetIndex`, indirect uses
   `iCommandBuffer`) so a future reader doesn't "fix" one to match the other without understanding.
4. **If a bug:** change `BindGraphicsDescriptorSets` to take a separate Set-0 framebuffer index (mirror
   `BindComputeDescriptorSets`'s `iCommandBuffer` + `iDescriptorSetIndex` split), and pass `iCommandBuffer` for
   Set 0 at the plain-draw call sites. Then the doc's "always framebuffer-indexed" becomes true.

## Out of scope

- **The compute path** (`BindComputeDescriptorSets`) — already framebuffer-indexes Set 0 per the doc; unchanged.
- **The indirect-graphics path** (`RecordDrawIndirect`) — already passes `iCommandBuffer` for Set 0; unchanged
  except to note it in the doc.
- **Reworking the per-pipeline Set 1 (`mbPerCommandBuffer`) logic** — that part of the doc/code already agrees;
  this plan only addresses the Set-0 graphics-plain-draw discrepancy.
- **Descriptor-set *layout* / set count** — three-set model (0 global / 1 per-pipeline / 2 per-material) is
  unchanged.

## Acceptance criteria

- A determination on record of whether plain-draw graphics Set-0 = `mGlobalDescriptorSets[0]` across all
  framebuffers is correct or a latent cross-frame hazard, with the reasoning (what Set 0 holds, whether it varies
  per framebuffer for the affected pipelines).
- If correct: `Objects/CLAUDE.md`'s "global Set 0 is always indexed by framebuffer" is reworded to the real
  per-path rule and the graphics plain-draw-vs-indirect inconsistency is noted.
- If a bug: `BindGraphicsDescriptorSets` framebuffer-indexes Set 0 on the plain-draw path (mirroring compute),
  the client builds, and the doc's "always framebuffer-indexed" is now accurate; no validation errors / GPU
  hazard in the affected pipelines.

## Critical files

- `Engine/Source/Graphics/Objects/Pipeline.cpp` — `BindGraphicsDescriptorSets`, `BindComputeDescriptorSets`,
  `RecordBindPipelineDescriptorsAndDraw`, `RecordBindPipelineAndDescriptors`, `RecordDrawIndirect`,
  `RecordCompute` / `RecordComputeIndirect` (the `iDescriptorSetIndex = mbPerCommandBuffer ? iCommandBuffer : 0`
  sites and the Set-0 index choices).
- `Engine/Source/Graphics/Objects/CLAUDE.md` — "Bind-time set indexing" claim (doc target if correct-as-is).
- Read-only context: `Engine/Source/Graphics/CLAUDE.md` (per-framebuffer descriptor sets, RenderGlobal
  descriptor-patch window), `Engine/Source/Graphics/Managers/CLAUDE.md` (eviction-symmetry / `mGlobalDescriptorSets`
  / `mImageInfos`).

## Notes

- **Investigation-first** (verify root cause before editing, per Diagnosis Discipline) — the deliverable could be
  doc-only (overstatement) or a real descriptor-binding fix; do not pick before establishing what Set 0 holds
  per framebuffer for the affected plain-draw pipelines.
- Client/graphics-only; no CRC/determinism/network exposure. If it is a real bug, the risk is a GPU
  use-after-patch / cross-frame conflict (visual corruption), not determinism.
- One grill decision staged: doc-overstatement vs latent-bug — resolved by the Set-0-content investigation in
  step 1-2.

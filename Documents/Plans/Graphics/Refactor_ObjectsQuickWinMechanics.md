# Refactor: Objects Quick-Win Mechanics

## Context

Source: /external-refactor-clean on `Engine/Source/Graphics/Objects` (non-recursive). Six small mechanical
fixes, batched: one silent-stack-overwrite guard at an array already at capacity, a per-frame
trust-boundary violation with unreachable code, two convention-deviating throws, a blanket warning
suppression with a narrow type fix, and two one-liners.

## Design

### Engine/Source/Graphics/Objects/PipelineCreator.cpp
- Bound the MRT blend-state array — `VkPipelineColorBlendAttachmentState pMrtBlendStates[6] {}` (`:580`)
  is written `[0, iColorAttachmentCount)` (`:583-586`) with no bound check, and the sole MRT caller already
  passes exactly 6 (`PipelineManager.cpp:167`); a 7th attachment is a silent stack overwrite. Add
  `static constexpr int64_t kiMaxColorAttachments {6};` +
  `ASSERT(iColorAttachmentCount <= kiMaxColorAttachments);`. [~5m]

### Engine/Source/Graphics/Objects/Pipeline.cpp
- `Pipeline::WriteIndirectBuffer` (`:328-332`): replace
  `if (mpIndirectMappedMemory == nullptr) { ASSERT(false); return; }` with
  `ASSERT(mpIndirectMappedMemory != nullptr);` — `ASSERT` unconditionally throws on failure in every build
  config (`Common/ErrorUtils.h:12`, `ErrorUtils.cpp:6-13`) so the `return` is unreachable, and the
  guard-then-bail shape violates the trust-boundary rule. Branch count is unchanged (`ASSERT` tests its own
  condition); the win is deleting the unreachable statement and stating the invariant directly. Callers are
  per-frame (`MainUniforms.cpp:180,182`, `SmokeUniforms.cpp:75-97`, collection `*Render.cpp`). [~5m]
- `Pipeline::RecordDrawIndirect`/`RecordComputeIndirect` (`:255-258`, `:309-312`): the
  `vmaGetAllocationInfo` calls exist only to feed the slot-bounds ASSERTs. Store the slot count on
  `Pipeline` when `SetupIndirectBuffer` sizes the buffer (`PipelineCreator.cpp:448`,
  `max(framebufferCount, 3)`) and assert `iCommandBuffer < miIndirectSlotCount` instead — clearer invariant,
  drops the VMA query (cold record path; optional, take or drop at grill). [~15m]

### Engine/Source/Graphics/Objects/Buffer.cpp
- `Buffer::RecordBarriers` (`:84`, `:112`): replace the two `throw std::runtime_error` on unhandled enum
  with the Graphics convention `default: ASSERT(false); break;` (precedent `AnimationData.cpp:295-296`).
  `ASSERT(false)` still throws after logging + `DEBUG_BREAK()` (`ErrorUtils.cpp:6-13`) — strictly better,
  convention-consistent. [~5m]

### Engine/Source/Graphics/Objects/Shader.h / Shader.cpp
- Remove the blanket `#pragma warning(push, 0)` / disable-26461 wrapping `Shader::Create`
  (`Shader.cpp:16-17,38`) by fixing the trigger: change `pData` to `const std::byte*` (`Shader.h:20,23`,
  `Shader.cpp:6,19`) and cast with `reinterpret_cast<const uint32_t*>` (`:29,:32` —
  `VkShaderModuleCreateInfo::pCode` is already `const uint32_t*`), then delete all three pragma lines. [~10m]

### Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp
- Qualify the bare `memcpy` (`:169`, the Materials lambda) as `std::memcpy` — matching the form the style
  guide itself uses (rule 60's example is `std::memcpy`; the rule's actual subject, gating size on the
  destination, is already satisfied here via `offsetof` on the destination type). [~2m]

## Critical files
- `Engine/Source/Graphics/Objects/PipelineCreator.cpp`
- `Engine/Source/Graphics/Objects/Pipeline.h`, `Pipeline.cpp`
- `Engine/Source/Graphics/Objects/Buffer.cpp`
- `Engine/Source/Graphics/Objects/Shader.h`, `Shader.cpp`
- `Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp`

## Out of scope
- The `WriteModelDescriptor`/`Write` decomposition — `Graphics/Refactor_WriteDescriptorDecomposition.md`.
- The `CreateGraphicsPipeline` decomposition — `Graphics/Refactor_PipelineCreatorDecomposition.md` (shares
  `PipelineCreator.cpp`; co-schedule or land this first — two inserted lines are easy to refresh).
- The `ModelPipeline.cpp:152` "Set 0" comment fix — `Graphics/Architecture_ObjectsShaderCpuConsistency.md`.
- `Buffer::CreateBuffer`'s 8-parameter signature and `Texture::CreateRenderTarget`'s length — high churn /
  low value, deliberately not filed.

## Acceptance criteria
- Client builds clean with the pragmas gone and no new warnings from `Shader.cpp`; behavior identical
  everywhere except the strictly-additive asserts.

## Notes
- Client-only; no determinism/CRC, `kiVersion`, replay, or network exposure. All items compile-checked;
  the `WriteIndirectBuffer` item touches a per-frame path but only removes a dead branch.
- One pre-staged grill choice: take or drop the optional `miIndirectSlotCount` simplification.

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source.
- MRT bound: `pMrtBlendStates[6]` at `PipelineCreator.cpp:580`, replication loop `:583-586` writes
  `[0, iColorAttachmentCount)` unchecked. Repo grep confirms the only explicit `>1` caller passes 6
  (`PipelineManager.cpp:167`); the lighting-pass auto-upgrade (`PipelineCreator.cpp:576-579`) produces 3 —
  both within the bound, so the assert is purely future-proofing.
- `ASSERT` semantics confirmed unconditional in all configs (`ErrorUtils.h:12` macro always evaluates and
  calls `common::Assert`, which logs, `DEBUG_BREAK()`s, and throws — `ErrorUtils.cpp:6-13`); the `return`
  at `Pipeline.cpp:331` is therefore unreachable everywhere, not just in debug. Corrected the original
  "drops a per-frame branch" claim — branch count is identical; the item is dead-statement removal +
  trust-boundary shape.
- Optional VMA item: query-feeding-assert pattern confirmed at `Pipeline.cpp:255-258` (`RecordDrawIndirect`)
  and `:309-312` (`RecordComputeIndirect`); slot count originates at `PipelineCreator.cpp:447-449`
  (`max(framebufferCount, 3)`). Note both Record sites are command-buffer *record* time (record-once
  renderer), not per-frame — as the plan already states.
- `Buffer::RecordBarriers` throws at `Buffer.cpp:84,112`; convention precedent
  `default: ASSERT(false); break;` confirmed at `AnimationData.cpp:295-297`.
- Shader pragma item exact: `#pragma warning(push, 0)` / `disable : 26461` at `Shader.cpp:16-17`, pop `:38`;
  `reinterpret_cast<uint32_t*>(pData)` at `:29` and `:32`; `VkShaderModuleCreateInfo::pCode` is
  `const uint32_t*`, and the sole producer passes pack-data pointers (`PipelineManager.cpp:54`), so the
  `const std::byte*` parameter change is a non-breaking widening (non-const converts implicitly).
- Bare `memcpy` confirmed at `PipelineDescriptorWriter.cpp:169`; rule-60 citation reworded (the rule is
  about size gating; the qualification matches its example form).

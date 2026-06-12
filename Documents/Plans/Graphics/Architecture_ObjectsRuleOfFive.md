# Architecture: Objects Rule-of-Five Discipline

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Objects` (non-recursive). Two of the six
resource-owning wrappers are fully copyable today: `Shader` and `CommandBuffers` declare no copy/move
control at all while owning raw Vulkan handles, so an accidental copy compiles silently and double-frees at
destruction. The other wrappers already enforce non-copyability (see Out of scope), so this plan closes the
two real gaps only.

## Design

### Engine/Source/Graphics/Objects/Shader.h
- Delete copies — `Shader` (`Shader.h:15-29`) deletes nothing and owns a raw `VkShaderModule`; a copy
  double-frees in `Destroy` (`Shader.cpp:40-47` destroys whenever the handle is non-null). Delete copy
  ctor/assign. No move pair needed today: the only container is
  `std::unordered_map<common::crc_t, Shader>` (`Managers/DynamicPipelines.h:45,74`), populated in place via
  `try_emplace` (`PipelineManager.cpp:54`) — node-based, never copies or moves elements. Add moves only if
  the compile demands it. [~10m]

### Engine/Source/Graphics/Objects/CommandBuffers.h / CommandBuffers.cpp
- Delete copies, add a move ctor — `CommandBuffers` (`CommandBuffers.h:13-34`) deletes nothing and owns a
  pool / three command buffers / three semaphores / a fence; it lives in `std::vector<CommandBuffers>`
  (`Managers/CommandBufferManager.h:20`), growth avoided only by the `reserve` at
  `CommandBufferManager.cpp:19` before the `emplace_back(i)` loop (`:20-23`). Deleting copies makes
  `emplace_back` ill-formed (it requires MoveInsertable even when capacity suffices), so add a move ctor
  that steals all handles and nulls the source. The destructor (`CommandBuffers.cpp:59-72`) currently has
  NO null-handle guards: the `vkDestroy*` calls are spec-legal no-ops on `VK_NULL_HANDLE`, but
  `vkFreeCommandBuffers` requires a *valid* `commandPool` — add an early return (or guard the free/destroy
  block) when `mVkCommandPool == VK_NULL_HANDLE` so a moved-from object destructs cleanly. [~15m]

## Critical files
- `Engine/Source/Graphics/Objects/Shader.h`
- `Engine/Source/Graphics/Objects/CommandBuffers.h`, `CommandBuffers.cpp`

## Out of scope
- `Buffer` — already complete (deleted copies + move ctor + move assign, `Buffer.h:57-60`); it is the
  template the others should match.
- `Pipeline` — already non-copyable in practice: the copy ctor is explicitly deleted (`Pipeline.h:111`) and
  the copy-assignment operator, while not explicitly deleted, is *implicitly* deleted because the by-value
  member `Buffer mModelMaterialsStorageBuffer` (`Pipeline.h:159`) has a deleted copy-assign — `a = b;` does
  not compile today. Adding an explicit `= delete` would be documentation-only; not filed.
- `Texture` — both copies explicitly deleted (`Texture.h:83-84`) and a user-declared move ctor
  (`Texture.h:85-95`); the move-assign is simply not declared, and `t = std::move(u)` already fails to
  compile (resolves to the deleted copy-assign). No compilable misuse exists; making the absence explicit
  is cosmetic; not filed.
- Any destructor/`Destroy` behavior change beyond tolerating moved-from null handles.
- Lifecycle/registration semantics — `Graphics/Architecture_PipelineRegistrationOwnership.md`.

## Acceptance criteria
- `Shader` and `CommandBuffers` no longer have implicitly-generated copy operations; client builds clean
  (the compiler arbitrates whether any move is actually required); boot + settings-recreate smoke unchanged.

## Notes
- Mechanical, compile-checked; client-only, no determinism/CRC, `kiVersion`, replay, or network exposure.
- `Shader::Destroy` already null-checks (`Shader.cpp:42`); `CommandBuffers`' destructor does not — the
  null-pool guard above is part of this plan, not optional.
- No grill decisions.

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source. Two original items dropped:
- **Dropped: `Pipeline` copy-assign deletion.** The original claim — "`a = b;` compiles and both objects
  destroy the same handles" — is false. `Pipeline` contains `Buffer mModelMaterialsStorageBuffer` by value
  (`Pipeline.h:159`) and `Buffer::operator=(const Buffer&)` is deleted (`Buffer.h:58`), which implicitly
  deletes `Pipeline`'s copy-assignment operator. The double-free scenario cannot compile; the proposed edit
  was explicitness-only.
- **Dropped: `Texture` move-assign completion.** With copies explicitly deleted (`Texture.h:83-84`) and a
  user-declared move ctor (`:85-95`), no move-assignment is declared and none is implicitly generated;
  `t = std::move(u)` fails to compile via the deleted copy-assign. The original item's own fallback
  ("explicitly delete it") concedes the change is cosmetic. `RenderTargetTextures.h:23-35` holds Textures
  by value but never assigns them.
- **Kept (corrected): `CommandBuffers`.** Container claims exact (`CommandBufferManager.h:20`,
  `reserve` `:19`, `emplace_back` `:22`); the MoveInsertable requirement makes the move ctor mandatory once
  copies are deleted. Correction to the original Notes ("Destructors already tolerate null handles via
  their `VK_NULL_HANDLE` checks"): `~CommandBuffers` (`CommandBuffers.cpp:59-72`) has no such checks, and
  `vkFreeCommandBuffers` with a null pool is invalid usage — the dtor guard is now an explicit work item.
- **Kept: `Shader`.** Class spans `Shader.h:15-29`; owns `mVkShaderModule`; sole container is the
  node-based `unordered_map` populated via `try_emplace`, so deleting copies compiles without adding moves.

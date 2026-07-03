# HDR Render Pass Depth WAW Availability

## Context

The HDR scene render pass `mHdrVkRenderPass` (`SwapchainManager::CreateRenderPass`, `Engine/Source/Graphics/Managers/SwapchainManager.cpp`) has an incoming EXTERNAL→0 subpass dependency whose memory (availability) scope omits the depth attachment's writes — a latent write-after-write hazard on the depth image's automatic layout transition. Verified against the Vulkan spec 2026-07-03.

Current dependency (first element of `pHdrVkSubpassDependencies`, `SwapchainManager.cpp:112-123`):

```cpp
VkSubpassDependency
{
	.srcSubpass = VK_SUBPASS_EXTERNAL,
	.dstSubpass = 0,
	.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	.srcAccessMask = 0,
	.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
	.dependencyFlags = 0,
},
```

The hazard:

- The pass's depth attachment (`SwapchainManager.cpp:53-64`) is `mDepthTexture` (created once in `CreateFramebuffers`, `SwapchainManager.cpp:358`, bound into the single shared `mHdrVkFramebuffer` at `:413`) — one image reused by every frame in flight, `initialLayout = VK_IMAGE_LAYOUT_UNDEFINED`, `loadOp = CLEAR`.
- Every frame writes it at EARLY/LATE_FRAGMENT_TESTS with `VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT`.
- Per the spec (Khronos Synchronization-Examples wiki): "Image layout transitions may perform read and write accesses on all memory bound to the image subresource range, so applications must ensure that all memory writes have been made available before a layout transition is executed" — a WAW hazard always requires a memory dependency; `UNDEFINED`/discard does **not** exempt it.
- Execution-only ordering exists (`srcStageMask` includes `COLOR_ATTACHMENT_OUTPUT`, which per-command follows the fragment-test stages, so prior depth writes are execution-ordered before the transition), but with `srcAccessMask = 0` the previous frame's depth writes are never made *available* before the automatic UNDEFINED→DEPTH_STENCIL_ATTACHMENT_OPTIMAL transition/clear. The sync-validation layer may flag this.
- The comment block at `SwapchainManager.cpp:109-111` documents only the color WAR/acquire rationale; the shape was carried over deliberately from the historical present pass and never gained the depth memory scope.

The pass's color attachments are not affected: the outgoing 0→EXTERNAL dependency (`SwapchainManager.cpp:124-133`) makes `COLOR_ATTACHMENT_WRITE` at `COLOR_ATTACHMENT_OUTPUT` available each frame, covering both the HDR resolve target and the multisample attachment (same stage/access class). Only depth-stencil writes are made available nowhere.

## Design

Extend the incoming dependency's scopes in `pHdrVkSubpassDependencies[0]` (`SwapchainManager.cpp:114-123`):

- `srcStageMask` += `VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT`
- `srcAccessMask` = `VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT` (from 0)
- `dstStageMask` += `VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT` (dst already has `EARLY_FRAGMENT_TESTS`; `dstAccessMask` already carries `DEPTH_STENCIL_ATTACHMENT_READ | WRITE` — no dst-access change needed)
- Update the comment at `SwapchainManager.cpp:109-111` to also state the depth WAW rationale (previous frame's depth writes made available before the UNDEFINED transition).

Strictly widens synchronization scopes — no behavioral change on hardware already ordering correctly, no new dependencies, no pipeline/descriptor changes.

### Sibling survey (verified this session)

Other render passes reusing shared attachments with `initialLayout = UNDEFINED`:

- **Present pass** (`SwapchainManager.cpp:185-194`): color-only, `srcAccessMask = 0` is the canonical swapchain-acquire pattern (acquire semaphore provides availability/visibility) — deliberately unchanged.
- **`Texture::CreateRenderTarget`** (`Engine/Source/Graphics/Objects/Texture.cpp:377-386`): has only an *outgoing* 0→EXTERNAL dependency; the incoming dependency is the implicit one (srcStage TOP_OF_PIPE, srcAccess 0). Its optional depth branch (`TextureFlags::kDepth`, depth attachment at `:340-351`, UNDEFINED initial + CLEAR, per-texture `mpDepthTexture` reused each frame) has the same shape as the HDR gap — but **no live caller sets `TextureFlags::kDepth`** (all RTT creations use `{kRenderPass}` only: `RenderTargetTextures.cpp:85/208/234/254/293/350/371`, `TextureCache.cpp:157`). Latent-only via API; add a comment at the depth attachment noting that a future `kDepth` user must add an explicit incoming dependency with depth src scopes (mirroring the HDR fix). Do not add the dependency now.
- **`RenderTargetTexturesLighting.cpp` lighting MRT (`:123-132`) and spread MRT (`:245-254`)**: outgoing-only explicit dependencies, UNDEFINED-initial CLEAR *color* attachments reused each frame. No depth-class gap: each instance's outgoing dependency makes `COLOR_ATTACHMENT_WRITE` available before the next instance's transition, and cross-frame ordering rides the frame semaphore chain. No change.

## Critical files

- `Engine/Source/Graphics/Managers/SwapchainManager.cpp` — `SwapchainManager::CreateRenderPass`, `pHdrVkSubpassDependencies[0]` (`:112-123`) and the comment above it (`:109-111`)
- `Engine/Source/Graphics/Objects/Texture.cpp` — `Texture::CreateRenderTarget`, comment-only rider at the depth `VkAttachmentDescription` (`:340-351`)

## Out of scope

- Adding explicit incoming EXTERNAL→0 dependencies to `Texture::CreateRenderTarget` or the RenderTargetTexturesLighting passes (comment rider only; no live depth caller, color availability already covered)
- Any change to the present pass's acquire-semaphore dependency
- Restructuring the outgoing 0→EXTERNAL dependencies or the semaphore chain
- Enabling/running the sync-validation layer (agents cannot run the game)

## Acceptance criteria

- `pHdrVkSubpassDependencies[0]` src scope covers EARLY/LATE fragment tests + `DEPTH_STENCIL_ATTACHMENT_WRITE`; dst stage covers both fragment-test stages
- Comment states the depth WAW rationale
- Client build compiles clean

## Notes

- Invariant exposure: none — client-only graphics (`Managers/` cpp, BT_CLIENT-wrapped), no determinism/CRC, no `kiVersion`/`.pack`, no wire, no shader repack.
- Pre-staged grill decision: the cross-frame semaphore chain (semaphore signal ops make all device writes available) arguably already provides availability implicitly on this single-queue renderer — the explicit mask extension is still the correct, self-documenting fix and is what silences sync-validation; confirm the user wants the fix regardless of that defense (recommended: yes — zero cost, spec-clean).

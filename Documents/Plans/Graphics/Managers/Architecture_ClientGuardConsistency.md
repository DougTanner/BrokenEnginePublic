# Architecture: BT_CLIENT Guard Consistency (Graphics/Managers)

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Managers` (non-recursive). The directory is
correctly client-only today (server vcxproj contains zero `Graphics\Managers` files; all manager headers sit
inside `Engine.h`'s single `BT_CLIENT` span at `Engine/Source/Engine.h:21-84`), but only 9 of 19 cpps are
whole-file `#if defined(BT_CLIENT)`-wrapped — the other 10 are client-only purely via vcxproj membership. An
unwrapped file accidentally added to the server vcxproj fails only at link (or silently compiles), with no
preprocessor tripwire. Root CLAUDE.md's rule ("files fully wrapped in `#if defined(BT_CLIENT)` must only
appear in the client vcxproj") is already satisfied by the wrapped nine; this plan extends the wrap to the
rest so the directory has one convention.

## Design

### Wrap the 10 unguarded cpps in `#if defined(BT_CLIENT)` … `#endif`
All in `Engine/Source/Graphics/Managers/` (mechanical; zero behavior change) [~30m total]:
- `DeviceManager.cpp`
- `InstanceManager.cpp`
- `SwapchainManager.cpp`
- `BufferManager.cpp`
- `CommandBufferManager.cpp`
- `CommandBufferRecordGlobal.cpp`
- `CommandBufferRecordMain.cpp`
- `TextManager.cpp`
- `TextureUploadManager.cpp`
- `ImGuiManager.cpp`

(Already wrapped, for reference: ParticleManager, DynamicPipelines, PipelineManager, RenderTargetTextures ×3,
TextureCache, TextureDescriptors, TextureManager.)

### Document the convention
- `Engine/Source/Graphics/CLAUDE.md:19` documents mixed guard scope for `Graphics.cpp`/`Islands`/`CameraBase`
  but does not cover the Managers split; after the wrap, add one line stating all `Managers/` cpps are
  whole-file `BT_CLIENT`-wrapped and client-vcxproj-only. [~5m]

## Critical files
- The 10 cpps listed above
- `Engine/Source/Graphics/CLAUDE.md`

## Out of scope
- vcxproj changes — all files are already client-only members (`BrokenEngineSandbox.vcxproj:371-387, 555-573`);
  no project edits needed.
- Narrowing any *intra-file* guard — there are none in the directory (verified; the only `BT_CLIENT` usage is
  whole-file wraps).
- `Engine.h`'s guard span — already correct.

## Acceptance criteria
- All 19 Managers cpps compile to empty TUs under `BT_SERVER`; client builds clean and unchanged.

## Notes
- No determinism/CRC/network exposure — preprocessor-only, compile-checked.
- Touches `TextManager.cpp`, `TextureManager.cpp`-adjacent files also edited by
  `Architecture_IncludeHygiene.md` — trivially mergeable, but co-scheduling avoids line-drift (File Groups).

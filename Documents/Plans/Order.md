# Plan Execution Order

Bugfixes, changes, and enhancements. Sorted by score (lowest = highest priority).

Score = Effort - Impact + Risks (lower = higher priority)

| # | Plan | Description | Effort | Impact | Risks | Score |
|---|------|-------------|--------|--------|-------|-------|
| 1 | Network/ServerPerformance.txt | Optimize buffer pruning O(n^2)->O(n) and defer debug serialization | 1 | 2 | 0 | -1 |
| 2 | Network/VisualSmoothing.txt | Decaying position offset for smooth visual reconciliation | 2 | 3 | 0 | -1 |
| 3 | Audio/MusicStreaming.txt | Fix StreamingVoice heap allocations, deduplicate transition logic | 2 | 2 | 0 | 0 |
| 4 | Graphics/ManagerAndShaderCleanup.txt | Assessment: managers/shader layouts appropriate, no changes needed | 0 | 0 | 0 | 0 |
| 5 | Misc/ReplaceMipmapGeneration.txt | Replace box-filter mipmaps with stb_image_resize2 | 2 | 3 | 1 | 0 |
| 6 | Misc/ReplaceShaderIncludeTracking.txt | Replace custom include parser with glslc depfiles | 2 | 2 | 0 | 0 |
| 7 | Network/FullStateTickValidation.txt | Reject stale full states, preserve unconsumed pending states | 2 | 2 | 0 | 0 |
| 8 | Network/NetworkCodeCleanup.txt | DRY: FindClient dedup, WriteGridCoord, SendPacket helpers | 1 | 1 | 0 | 0 |
| 9 | Network/NetworkHardening.txt | static_assert, CRC renames, DEBUG_BREAK removal | 1 | 1 | 0 | 0 |
| 10 | Network/SubscriptionCancel.txt | Track cancelled subscriptions to avoid wasted bandwidth | 1 | 1 | 0 | 0 |
| 11 | Frame/CombineCrcs.txt | Merge Crc() and ServerCrc() into Crcs() returning std::pair | 2 | 2 | 1 | 1 |
| 12 | Frame/DuplicationFixes.txt | Remove duplicated TimeStep text, templatize InterpolateKeyframes | 2 | 2 | 1 | 1 |
| 13 | Graphics/ArchitectureCleanupPlan.txt | Loop-based pipeline/texture creation, static helper for MRT | 2 | 2 | 1 | 1 |
| 14 | Graphics/DrawCallBatchingPlan.txt | Remove redundant vertex buffer binds in model rendering | 1 | 1 | 1 | 1 |
| 15 | Graphics/LightingBlurAndBarriersPlan.txt | kFragmentShaderReadOnly layout, interleave blur, extract helper | 2 | 2 | 1 | 1 |
| 16 | Graphics/PipelineCacheAndCompilation.txt | Add VkPipelineCache, convert file-static mutables to locals | 3 | 3 | 1 | 1 |
| 17 | Misc/ReplaceCmftWithCmgen.txt | Replace unmaintained CMFT with Google cmgen for IBL | 2 | 2 | 1 | 1 |
| 18 | Misc/ReplaceOpenExrWithTinyexr.txt | Replace heavyweight OpenEXR with single-header tinyexr | 2 | 2 | 1 | 1 |
| 19 | Misc/ReplaceTinyGltfWithFastgltf.txt | Replace TinyGLTF with faster fastgltf library | 2 | 2 | 1 | 1 |
| 20 | Graphics/TextureUploadPipelinePlan.txt | Double-buffer staging resources for texture uploads | 3 | 2 | 1 | 2 |
| 21 | Frame/CollisionRefactor.txt | Deduplicate zone logic, generation counter, extract area damage | 4 | 3 | 2 | 3 |
| 22 | Frame/FileSplits.txt | Split 6 oversized files into focused units | 4 | 3 | 2 | 3 |
| 23 | Frame/FramePurityFixes.txt | Remove singleton access from Frame code | 5 | 4 | 2 | 3 |
| 24 | Graphics/VMAAndMemoryPlan.txt | VMA budget query, merge small buffers, consolidate uniforms | 3 | 2 | 2 | 3 |
| 25 | Graphics/VisualQualityPlan.txt | Default MSAA 4x, separable shadow blur, alpha-aware shadows | 4 | 3 | 2 | 3 |

## Dependencies

Plans that must be executed in order due to shared files or stale line numbers:

## File Groups

Plans that touch the same files and should be done in a single session:

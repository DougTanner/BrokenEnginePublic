# Plan Execution Order

Bugfixes, changes, and enhancements. Sorted by score (lowest = highest priority).

Score = Effort - Impact + Risks (lower = higher priority)

| # | Plan | Description | Effort | Impact | Risks | Score |
|---|------|-------------|--------|--------|-------|-------|
| 1 | Network/ServerPerformance.txt | Optimize buffer pruning O(n^2)->O(n) and defer debug serialization | 1 | 2 | 0 | -1 |
| 2 | Network/VisualSmoothing.txt | Decaying position offset for smooth visual reconciliation | 2 | 3 | 0 | -1 |
| 3 | Misc/ReplaceMipmapGeneration.txt | Replace box-filter mipmaps with stb_image_resize2 | 2 | 3 | 1 | 0 |
| 4 | Misc/ReplaceShaderIncludeTracking.txt | Replace custom include parser with glslc depfiles | 2 | 2 | 0 | 0 |
| 5 | Network/NetworkCodeCleanup.txt | DRY: FindClient dedup, WriteGridCoord, SendPacket helpers | 1 | 1 | 0 | 0 |
| 6 | Frame/DuplicationFixes.txt | Remove duplicated TimeStep text, templatize InterpolateKeyframes | 2 | 2 | 1 | 1 |
| 7 | Graphics/LightingBlurAndBarriersPlan.txt | kFragmentShaderReadOnly layout, interleave blur, extract helper | 2 | 2 | 1 | 1 |
| 8 | Misc/ReplaceCmftWithCmgen.txt | Replace unmaintained CMFT with Google cmgen for IBL | 2 | 2 | 1 | 1 |
| 9 | Misc/ReplaceOpenExrWithTinyexr.txt | Replace heavyweight OpenEXR with single-header tinyexr | 2 | 2 | 1 | 1 |
| 10 | Misc/ReplaceTinyGltfWithFastgltf.txt | Replace TinyGLTF with faster fastgltf library | 2 | 2 | 1 | 1 |
| 11 | Graphics/TextureUploadPipelinePlan.txt | Double-buffer staging resources for texture uploads | 3 | 2 | 1 | 2 |
| 12 | Frame/FileSplits.txt | Split 6 oversized files into focused units | 4 | 3 | 2 | 3 |
| 13 | Frame/FramePurityFixes.txt | Remove singleton access from Frame code | 5 | 4 | 2 | 3 |
| 14 | Graphics/VMAAndMemoryPlan.txt | VMA budget query, merge small buffers, consolidate uniforms | 3 | 2 | 2 | 3 |

## Dependencies

Plans that must be executed in order due to shared files or stale line numbers:

## File Groups

Plans that touch the same files and should be done in a single session:

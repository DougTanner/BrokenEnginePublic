# Plan Execution Order

Sorted from least effort / lowest risk to largest effort / highest risk.

## Tier 1: Quick Wins (Small effort, Low risk)

1. **Network/SubscriptionCancel.txt** - Track cancelled coords to skip incoming full states (3 files)
2. **Graphics/ManagerAndShaderCleanup.txt** - YAGNI/no action needed, already validated as not worth doing
3. **Graphics/DrawCallBatchingPlan.txt** - Phase 1 only: remove redundant vertex buffer binds (1 change)
4. **Graphics/ArchitectureCleanupPlan.txt** - Terrain data/texture loops, MRT consolidation (~80 lines removed)
5. **Audio/MusicStreaming.txt** - Replace heap vectors with std::array, extract helper (4 changes)

## Tier 2: Moderate Effort, Low Risk

6. **Network/ServerPerformance.txt** - O(n^2) buffer pruning fix, deferred debug serialization (2 changes)
7. **Frame/DuplicationFixes.txt** - Extract helpers, templatize keyframes, remove UUID duplication (7 files)
8. **Graphics/PipelineCacheAndCompilation.txt** - VkPipelineCache add, remove static mutable structs
9. **Graphics/TextureUploadPipelinePlan.txt** - Double-buffer staging, shared transient command pool
10. **Graphics/VMAAndMemoryPlan.txt** - Budget query ImGui, merge tiny buffers, merge uniform buffers

## Tier 3: Moderate Effort, Moderate Risk

11. **Graphics/VisualQualityPlan.txt** - MSAA 4x default, separable shadow blur, alpha shadow casting
12. **Graphics/LightingBlurAndBarriersPlan.txt** - New TextureLayout value, extract blur helper, interleave loops (13 steps)
13. **Network/ForwardErrorCorrection.txt** - XOR parity FEC for unreliable channels (~130 lines, 7 files)
14. **Network/HermiteInterpolation.txt** - Cubic Hermite spline for render smoothing (8 collections)
15. **Network/VisualSmoothing.txt** - Per-coord visual error offset with decay for reconciliation (5 files)
16. **Network/RemoteEntityInterpolation.txt** - Ring buffer of confirmed states, render at interpolation delay (3 files)
17. **Network/NetworkMetricsAndAdaptiveBuffering.txt** - Packet loss/jitter tracking, adaptive throttle

## Tier 4: Large Effort or Higher Risk

18. **Frame/CombineCrcs.txt** - Merge Crc()/ServerCrc() into Crcs(), 40+ call sites across 5 files
19. **Network/SoftDesyncRecovery.txt** - Recovery instead of disconnect, frequency tracking (10 files, control flow changes)
20. **Frame/FileSplits.txt** - Split 6 large files into smaller units, create 4+ new files (large but low risk)
21. **Frame/CollisionRefactor.txt** - Deduplicate zone logic, generation counter, extract AreaDamage collection (5 changes)
22. **Frame/FramePurityFixes.txt** - Remove global singleton access from Frame update, deferred event buffers

## Tier 5: Architectural / High Risk

23. **Frame/FrameRelativePositions.txt** - All positions frame-relative, requires MergeFramesForRender (13+ files, architectural shift)

## Reference Only (No code changes)

- **Misc/LinterToolingForStyleGuide.txt** - Analysis of automatable style rules, tooling recommendations

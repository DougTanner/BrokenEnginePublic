# Plan Execution Order

Sorted from least effort / lowest risk to largest effort / highest risk.

## Tier 1: Quick Wins (Small effort, Low risk)

1. **Network/NetworkCodeCleanup.txt** - FindClient dedup, WriteGridCoord helper, SendPacket helper (cleanup only)
2. **Network/FullStateTickValidation.txt** - Reject stale full states, assert tick match, clamp stale injection (3 files)
3. **Network/SubscriptionCancel.txt** - Track cancelled coords to skip incoming full states (3 files)
4. **Graphics/ManagerAndShaderCleanup.txt** - YAGNI/no action needed, already validated as not worth doing
5. **Graphics/DrawCallBatchingPlan.txt** - Phase 1 only: remove redundant vertex buffer binds (1 change)
6. **Graphics/ArchitectureCleanupPlan.txt** - Terrain data/texture loops, MRT consolidation (~80 lines removed)
7. **Audio/MusicStreaming.txt** - Replace heap vectors with std::array, extract helper (4 changes)

## Tier 2: Moderate Effort, Low Risk

9. **Network/ServerPerformance.txt** - O(n^2) buffer pruning fix, deferred debug serialization (2 changes)
10. **Frame/DuplicationFixes.txt** - Extract helpers, templatize keyframes, remove UUID duplication (7 files)
11. **Graphics/PipelineCacheAndCompilation.txt** - VkPipelineCache add, remove static mutable structs
12. **Graphics/TextureUploadPipelinePlan.txt** - Double-buffer staging, shared transient command pool
13. **Graphics/VMAAndMemoryPlan.txt** - Budget query ImGui, merge tiny buffers, merge uniform buffers

## Tier 3: Moderate Effort, Moderate Risk

14. **Graphics/VisualQualityPlan.txt** - MSAA 4x default, separable shadow blur, alpha shadow casting
15. **Graphics/LightingBlurAndBarriersPlan.txt** - New TextureLayout value, extract blur helper, interleave loops (13 steps)
16. **Network/ForwardErrorCorrection.txt** - XOR parity FEC for unreliable channels (~130 lines, 7 files)
17. **Network/HermiteInterpolation.txt** - Cubic Hermite spline for render smoothing (8 collections)
18. **Network/VisualSmoothing.txt** - Per-coord visual error offset with decay for reconciliation (5 files)
19. **Network/RemoteEntityInterpolation.txt** - Ring buffer of confirmed states, render at interpolation delay (3 files)
20. **Network/NetworkMetricsAndAdaptiveBuffering.txt** - Packet loss/jitter tracking, adaptive throttle

## Tier 4: Large Effort or Higher Risk

21. **Frame/CombineCrcs.txt** - Merge Crc()/ServerCrc() into Crcs(), 40+ call sites across 5 files
22. **Network/SoftDesyncRecovery.txt** - Recovery instead of disconnect, frequency tracking (10 files, control flow changes)
23. **Frame/FileSplits.txt** - Split 6 large files into smaller units, create 4+ new files (large but low risk)
24. **Frame/CollisionRefactor.txt** - Deduplicate zone logic, generation counter, extract AreaDamage collection (5 changes)
25. **Frame/FramePurityFixes.txt** - Remove global singleton access from Frame update, deferred event buffers

## Tier 5: Architectural / High Risk

26. **Frame/FrameRelativePositions.txt** - All positions frame-relative, requires MergeFramesForRender (13+ files, architectural shift)

## Reference Only (No code changes)

- **Misc/LinterToolingForStyleGuide.txt** - Analysis of automatable style rules, tooling recommendations

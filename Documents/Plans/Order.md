# Plan Execution Order

Sorted from least effort / lowest risk to largest effort / highest risk.

## Tier 2: Moderate Effort, Low Risk

1. **Network/ServerPerformance.txt** - O(n^2) buffer pruning fix, deferred debug serialization (2 changes)
2. **Frame/DuplicationFixes.txt** - Extract helpers, templatize keyframes, remove UUID duplication (7 files)
3. **Graphics/PipelineCacheAndCompilation.txt** - VkPipelineCache add, remove static mutable structs
4. **Graphics/TextureUploadPipelinePlan.txt** - Double-buffer staging, shared transient command pool
5. **Graphics/VMAAndMemoryPlan.txt** - Budget query ImGui, merge tiny buffers, merge uniform buffers

## Tier 3: Moderate Effort, Moderate Risk

6. **Graphics/VisualQualityPlan.txt** - MSAA 4x default, separable shadow blur, alpha shadow casting
7. **Graphics/LightingBlurAndBarriersPlan.txt** - New TextureLayout value, extract blur helper, interleave loops (13 steps)
8. **Network/ForwardErrorCorrection.txt** - XOR parity FEC for unreliable channels (~130 lines, 7 files)
9. **Network/HermiteInterpolation.txt** - Cubic Hermite spline for render smoothing (8 collections)
10. **Network/VisualSmoothing.txt** - Per-coord visual error offset with decay for reconciliation (5 files)
11. **Network/RemoteEntityInterpolation.txt** - Ring buffer of confirmed states, render at interpolation delay (3 files)
12. **Network/NetworkMetricsAndAdaptiveBuffering.txt** - Packet loss/jitter tracking, adaptive throttle

## Tier 4: Large Effort or Higher Risk

13. **Frame/CombineCrcs.txt** - Merge Crc()/ServerCrc() into Crcs(), 40+ call sites across 5 files
14. **Network/SoftDesyncRecovery.txt** - Recovery instead of disconnect, frequency tracking (10 files, control flow changes)
15. **Frame/FileSplits.txt** - Split 6 large files into smaller units, create 4+ new files (large but low risk)
16. **Frame/CollisionRefactor.txt** - Deduplicate zone logic, generation counter, extract AreaDamage collection (5 changes)
17. **Frame/FramePurityFixes.txt** - Remove global singleton access from Frame update, deferred event buffers

## Tier 5: Architectural / High Risk

18. **Frame/FrameRelativePositions.txt** - All positions frame-relative, requires MergeFramesForRender (13+ files, architectural shift)

## Reference Only (No code changes)

- **Misc/LinterToolingForStyleGuide.txt** - Analysis of automatable style rules, tooling recommendations

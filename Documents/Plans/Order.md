# Plan Execution Order

Sorted from least effort / lowest risk to largest effort / highest risk.

## Tier 2: Moderate Effort, Low Risk

1. **Graphics/TextureUploadPipelinePlan.txt** - Double-buffer staging, shared transient command pool
2. **Graphics/VMAAndMemoryPlan.txt** - Budget query ImGui, merge tiny buffers, merge uniform buffers

## Tier 3: Moderate Effort, Moderate Risk

3. **Graphics/VisualQualityPlan.txt** - MSAA 4x default, separable shadow blur, alpha shadow casting
4. **Graphics/LightingBlurAndBarriersPlan.txt** - New TextureLayout value, extract blur helper, interleave loops (13 steps)
5. **Network/ForwardErrorCorrection.txt** - XOR parity FEC for unreliable channels (~130 lines, 7 files)
6. **Network/HermiteInterpolation.txt** - Cubic Hermite spline for render smoothing (8 collections)
7. **Network/VisualSmoothing.txt** - Per-coord visual error offset with decay for reconciliation (5 files)
8. **Network/RemoteEntityInterpolation.txt** - Ring buffer of confirmed states, render at interpolation delay (3 files)
9. **Network/NetworkMetricsAndAdaptiveBuffering.txt** - Packet loss/jitter tracking, adaptive throttle

## Tier 4: Large Effort or Higher Risk

10. **Frame/CombineCrcs.txt** - Merge Crc()/ServerCrc() into Crcs(), 40+ call sites across 5 files
11. **Network/SoftDesyncRecovery.txt** - Recovery instead of disconnect, frequency tracking (10 files, control flow changes)
12. **Frame/FileSplits.txt** - Split 6 large files into smaller units, create 4+ new files (large but low risk)
13. **Frame/CollisionRefactor.txt** - Deduplicate zone logic, generation counter, extract AreaDamage collection (5 changes)
14. **Frame/FramePurityFixes.txt** - Remove global singleton access from Frame update, deferred event buffers

## Tier 5: Architectural / High Risk

15. **Frame/FrameRelativePositions.txt** - All positions frame-relative, requires MergeFramesForRender (13+ files, architectural shift)

## Reference Only (No code changes)

- **Misc/LinterToolingForStyleGuide.txt** - Analysis of automatable style rules, tooling recommendations

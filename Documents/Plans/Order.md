# Plan Execution Order

Sorted from least effort / lowest risk to largest effort / highest risk.

## Tier 3: Moderate Effort, Moderate Risk

1. **Network/NetworkMetricsAndAdaptiveBuffering.txt** - Packet loss/jitter tracking, adaptive throttle

## Tier 4: Large Effort or Higher Risk

2. **Frame/CombineCrcs.txt** - Merge Crc()/ServerCrc() into Crcs(), 40+ call sites across 5 files
3. **Network/SoftDesyncRecovery.txt** - Recovery instead of disconnect, frequency tracking (10 files, control flow changes)
4. **Frame/FileSplits.txt** - Split 6 large files into smaller units, create 4+ new files (large but low risk)
5. **Frame/CollisionRefactor.txt** - Deduplicate zone logic, generation counter, extract AreaDamage collection (5 changes)
6. **Frame/FramePurityFixes.txt** - Remove global singleton access from Frame update, deferred event buffers

## Tier 5: Architectural / High Risk

7. **Frame/FrameRelativePositions.txt** - All positions frame-relative, requires MergeFramesForRender (13+ files, architectural shift)

## Reference Only (No code changes)

- **Misc/LinterToolingForStyleGuide.txt** - Analysis of automatable style rules, tooling recommendations

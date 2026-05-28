# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/

AI-controlled enemy spaceships with health, weapons, freeze time, and per-instance skeletal animation. `Register()` also registers the enemy blaster type (camera-aligned point light) fired by spaceships.

## Game-Specific Behavior

- **Sizing**: `kfSpaceshipRadius` is the single source of truth for body size; pusher, explosions, blaster, target, hit-flash, terrain displacement, and model scale all derive from it.
- **AI targeting hierarchy**: Return-to-island-center > flee-from-player (with hysteresis) > chase nearest alive player. Island center is the *nearest* per-cell island placement, not a fixed point. Steering uses two-stage exponential decay, clamped to max turn rate after terrain bounce and terrain avoidance.
- **Two damage paths**: Direct hits from player blasters land via the collision system (PreCollision registers a layer, PostCollision reads results); missile splash lands via the `AreaDamage` phase. Health regenerates while far from the nearest player. Reaching zero health begins a death sequence (knockback opposite the damage direction, staggered explosions over the destroyed-time countdown).
- **Freeze time**: While frozen, `Interpolate::Update` skips velocity integration; Render emits an additive warm-red color ramped by remaining freeze duration.
- **Destroyed-time sentinel**: `-1.0f` alive, `> 0.0f` counting through death animation, `== 0.0f` Destroy-eligible.
- **Static PostRender fields**: Damage directions and alignments are `memcpy`'d in `AllocateAndCopy` and written only at state transitions (exempt from unconditional-store rule).

## Render Pipeline (client)

- **Cross-coord accumulator** `siRendered` is file-static (not `thread_local`): assumes per-frame `Render` calls run sequentially on the render thread across active coords; parallelizing coord renders would race.
- **Two-pass render**: Pass 1 (main thread) culls and packs a compacted visible-indices list into the workbuffer, then reserves mesh-data and joint-matrix slabs covering all visible ships. Pass 2 dispatches workers writing deterministic non-overlapping slabs (lock-free).
- **Model swap site**: `kSpaceshipModel` in `SpaceshipsRender.cpp` is `#if 1`/`#if 0`-gated with a paired scale. `Spaceships.cpp` forward-declares it (client-only) for the animation-duration lookup.

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)

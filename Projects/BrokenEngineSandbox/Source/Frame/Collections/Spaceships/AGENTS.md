# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/

AI-controlled enemy spaceships with health, weapons, freeze time, and per-instance skeletal animation. `Register()` registers three types: spaceship explosion, enemy blaster (with camera-aligned point light on client), and a client-only hit-flash point-light controller. The blaster and hit-flash registrations are idempotent via `0xFF` sentinel guards; the explosion registration is unguarded (engine `RegisterType` asserts on re-registration).

## Game-Specific Behavior

- **Sizing**: `kfSpaceshipRadius` is the single source of truth for body size; pusher, explosions, blaster, terrain displacement, and model scale all derive from it.
- **AI targeting hierarchy**: Return-to-island-center > flee-from-player > chase nearest alive player; both flee and return toggle on distance hysteresis. Island center is the *nearest* per-cell island placement, not a fixed point. Steering uses two-stage exponential decay, clamped to max turn rate after terrain bounce and terrain avoidance.
- **Cross-phase field ownership**: Delta rotation and freeze time live in the Interpolate struct (Interpolate Update and Render read them) but are decayed and written by PostRender Update.
- **Spawn phase misnomer**: The PostRender Spawn phase fires enemy blasters (target player must be on-screen via `FrameInterpolate::IsVisible`, within a narrow facing cone, on cooldown, and the firing ship not already mid-death-explosion) and emits staggered death explosions — it never creates spaceships. Creation is the `SpawnInfo` overload of `Spawn`, called externally from `Frame.cpp` (chevron-group spawner) and `SpawnTransfer.cpp`.
- **Two damage paths**: Direct hits from player blasters land via the collision system (PreCollision registers a layer, PostCollision reads results); missile splash lands via the `AreaDamage` phase. Health regenerates while far from the nearest player. Reaching zero health begins a death sequence (knockback opposite the damage direction, staggered explosions over the destroyed-time countdown).
- **Freeze time**: While frozen, `Interpolate::Update` skips velocity integration; Render emits an additive warm-red color ramped by remaining freeze duration.
- **Destroyed-time sentinel**: `-1.0f` alive, `> 0.0f` counting through death animation, `== 0.0f` Destroy-eligible.
- **Static PostRender fields**: Damage directions and alignments are `memcpy`'d in `AllocateAndCopy` and written only at state transitions (exempt from unconditional-store rule).

## Render Pipeline (client)

- **Cross-coord accumulator** `siRendered` is file-static (not `thread_local`): per-frame `Render` calls must run sequentially on the main thread across active coords (re-entry assert-enforced); parallelizing coord renders would race.
- **Two-pass render**: Pass 1 (main thread) culls and packs a compacted visible-indices list into the workbuffer, then reserves mesh-data and joint-matrix slabs covering all visible ships. Pass 2 dispatches workers writing deterministic non-overlapping slabs (lock-free).
- **Model swap site**: `kSpaceshipModel` in `SpaceshipsRender.cpp` is `#if 1`/`#if 0`-gated with a paired scale. `Spaceships.cpp` forward-declares it (client-only) for the animation-duration lookup.

## See Also
- Parent collections: `../AGENTS.md`

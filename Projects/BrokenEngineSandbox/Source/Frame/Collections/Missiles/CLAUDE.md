# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/

Guided homing missiles with area-of-effect damage. Each missile owns a client-only area light, smoke trail, and sound.

## Game-Specific Behavior

- **Two-phase homing**: An initial boost ramps rotation strength from zero to full over a short delay; active tracking then orients toward the target with exponential smoothing. Targetless missiles orient toward the stored direction and re-attempt acquisition every Tick (alignment-aware Targets scan, same path used at spawn); homing on a freshly-acquired target engages the next Tick to match the read-previous / write-current pattern. The boost-ramp delay is preserved across re-acquisition (no spawn-style soft launch on re-target).
- **Area damage model**: Missiles deal zero direct collision damage — all damage is dealt through AoE explosions with a per-type configurable radius. Collision always registers per-element damage 0; exploding entries use `kAlreadyCollided`, others `kDestroyOnCollide`.
- **Target subscription lifetime**: Each Update tick must verify the target still exists in the Targets id map AND still carries the destination flag. On either failure, release the subscription, clear the id, and snapshot the current direction as the fallback orientation. Targetless missiles (just-cleared or never-acquired) re-attempt acquisition the same Tick via the alignment-aware Targets scan, which adds a new subscription on success. Transfer/Destroy release the subscription only if the target still exists.
- **Explode is idempotent**: Guarded by an `kExploding` early-out. Spawns three staggered explosions (full/half/quarter with position jitter), registers AoE via `engine::AreaDamage::Add`, removes area light and sound immediately — smoke trail persists through the destroy animation. Directional (terrain) hits sample `gpIslandTerrain->GlobalNormal` for hemispherical trail/particle spread.
- **AreaDamage() phase is intentionally empty**: AoE registration happens inside Explode at hit time (PostCollision/Destroy). The phase exists only to satisfy the collection interface.
- **Destroyed-time sentinel**: `-1` = alive, `> 0` = death-animation countdown (decayed in Interpolate Update, scales render size), `0` = finished — render skips the entry and Destroy removes it.
- **Static-at-spawn fields**: Flags, explosion direction, explosion radius, delta-rotation-max, acceleration, pitch, alignment (and sound on client) are `memcpy`'d in `AllocateAndCopy` and NOT re-stored by Update — overriding the parent's unconditional-store rule. Mutated only by Explode (flags + explosion direction).
- **Smoke-trail reuse across cell transfer**: The smoke-trail id is carried in `TransferData` so the destination cell re-binds the existing visual trail via `ClientInit`. Area light and sound are re-created fresh on arrival.

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)

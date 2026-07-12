# Missile Impact Trail Phase Separation

## Context

Exact missile impact handling now assigns the resolved entity or terrain contact in `MissilesPostRender::PostCollision`, then calls `SyncMissileTrail(rFrame.interpolate, ...)` (`Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/MissilesUpdate.cpp:320-359`). That helper calls `engine::SmokeTrailsInterpolate::Sync` (`Missiles.cpp:86-98`). This crosses the strict FrameInterpolate-sync / FramePostRender-collision boundary documented at `Projects/BrokenEngineSandbox/Source/Frame/AGENTS.md:21`; SmokeTrails' smoothed positions are persistent sim-frame state, not render-only state (`Engine/Source/Frame/Collections/SmokeTrails/AGENTS.md:3-8`).

This accepted step-11 residual was explicitly queued by the user rather than fixed in the collision session. Preserve the exact impact endpoint without running an Interpolate-owned Sync operation from PostCollision. The authoritative missile impact-position assignments remain collision response and are not the defect.

## Design

1. Keep entity/terrain impact resolution and missile position correction in `MissilesPostRender::PostCollision` so `Explode`, area damage, transfer, and rendering observe the accepted contact.
2. Remove both PostCollision `SyncMissileTrail` calls and remove or narrow that helper so `SmokeTrailsInterpolate::Sync` is reached only from Interpolate-owned synchronization.
3. First prove whether existing `MissilesInterpolate::Update` / `SyncMissile` consumes the corrected current-frame position in fixed-tick and variable-dt render interpolation soon enough to preserve the exact visible endpoint. Prefer this zero-state route.
4. Only if that path produces one-frame overshoot, carry a client-only pending impact endpoint from PostCollision and consume/clear it inside `MissilesInterpolate::Update`. Add no frame phase and no Spawn work; if SOA layout changes, invoke `/add-collection-member` and reassess version exposure.
5. Update missile/smoke-trail documentation to describe the phase-safe handoff.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/MissilesUpdate.cpp` — `MissilesInterpolate::Update`, `MissilesPostRender::PostCollision`
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.cpp` — `SyncMissile`, `SyncMissileTrail`
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.h` — only if a client-only handoff field is required
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/AGENTS.md` and `Engine/Source/Frame/Collections/SmokeTrails/AGENTS.md` — phase-safe endpoint contract

## Out of scope

- Moving missile or player-weapon creation out of Spawn, adding a projectile-emission phase, or changing same-tick spawn collision behavior.
- Changing collision ordering/cutoffs, missile impact position, explosion/AoE timing, transfer, or destruction.
- General phase-hook refactoring (`Frame/Architecture_PhaseHookOptIn.md`) or smoke-trail rendering/smoothing redesign.
- Unit tests.

## Acceptance criteria

- No PostRender hook reaches `SmokeTrailsInterpolate::Sync`.
- Entity and terrain impacts leave missile and visible trail head at the same resolved contact, without overshoot or one-frame endpoint lag.
- Weapon creation remains in Spawn; collision, transfer, destroy, and AoE ordering remain unchanged.
- Client/server builds pass; client entity/terrain impact smoke and replay/reconciliation smoke show trail continuity and unchanged shared CRC behavior.

## Notes

- **Determinism/CRC:** offending calls are client-only, but SmokeTrails positions persist in sim-frame state. Keep any handoff client-only and outside shared CRC; shared missile collision response stays unchanged.
- **Layout/versioning:** preferred deletion-only route needs no `kiVersion`, wire, save, `.pack`, or shader change. Reassess if a client-only SOA member proves necessary.
- **Allocation:** add no tick-path heap container.
- **Origin:** user directed follow-up plan, not an in-session fix.

# Pushers - Physics Force Fields

Radial repulsion force fields for entity separation (anti-stacking), owned by game entities (players, spaceships) via the hub's Sync pattern. Render path is client-only profile-counter tracking — no pipeline, no draw calls.

## Unique Aspects

- Carry-forward Update: Interpolate `Update` copies the full `Members()` tuple forward inside the existing Update phase — pushers have no self-driven simulation; last-synced state stays valid until the owner re-syncs. Do not move this copy into `AllocateAndCopy`.
- Zone acceleration: spatial grid recentered on player 0 and rebuilt by `SetupZones`, which the engine calls at the tail of `FramePostRenderBase::Update` after all collection Updates — zones are valid for PreCollision and later phases on the same thread only. Stored `thread_local` so each Dispatch worker and reconcile thread owns its copy. Arena 400m / 8m zones / cap 512 per zone (overflow `DEBUG_BREAK`); out-of-arena pushers silently skipped. A pusher spanning multiple zones is registered in every overlapping zone; a query reads only the single zone containing its position (out-of-arena queries clamp to the border zone).
- Push falloff: `(1 - d²/r²)^power * intensity`, directed pusher-to-query, summed across every pusher in the query's zone (not max-blended). Query skips a self-ignore id and any pusher coincident with the query point. Include/exclude masks on `PusherFlags_t` (default include `kTypeDefault`, exclude `kTypeMines`) — new pusher types opt in/out via a new `PusherFlags` enum value.
- ApplyClampedPush (`Pushers.h`): inline impulse that caps the velocity component in push direction, preventing stacking beyond caller-supplied max. General helper — game code also uses it for non-pusher impulses (terrain-normal push).

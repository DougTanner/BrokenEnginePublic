# Pushers - Physics Force Fields

Physics force fields with zone-based spatial acceleration. Compiles in both builds; render path is client-only profile-counter tracking (no draw calls).

## Unique Aspects

- **Owner-driven lifetime**: Pushers have no self-driven simulation — PostRender phase hooks are intentionally empty and owners mint/sync/release. Parent transfers owned pushers.
- **Zone acceleration**: Per-frame spatial grid centered on player 0, stored `thread_local` for Dispatch-worker isolation. Arena 400m / 8m zones / cap 512 per zone (overflow `DEBUG_BREAK`); out-of-arena pushers silently skipped.
- **Push falloff**: `(1 - d²/r²)^power * intensity`, directed pusher-to-query. Include/exclude masks on `PusherFlags_t` (default include `kTypeDefault`, exclude `kTypeMines`) — new pusher types opt in/out via a new `PusherFlags` enum value.
- **ApplyClampedPush** (`Pushers.h`): inline impulse that caps the velocity component in push direction, preventing stacking beyond caller-supplied max.

## See Also
- [../CLAUDE.md](../CLAUDE.md) - Collection<T>, SOA, Sync pattern, file splitting
</content>
</invoke>
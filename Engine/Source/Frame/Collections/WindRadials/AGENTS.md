# WindRadials - Controlled Wind Deposits

Client-only wind radials are stationary, fire-and-forget splats spawned by explosions. Their short controller lifetime deposits outward impulse into the GPU wind field.

## Invariants

- Controller keyframes are multipliers for each instance's base intensity and size; no normalization invariant is enforced.
- `PersistentMembers()` copies controller/start-time metadata and the base intensity/size. When wind is enabled, `Update` carries position from the previous frame and recomputes derived intensity/size.
- Disabling wind skips animation update and rendering, but expiry still runs. A row surviving a disable/enable transition can read stale previous-frame derived data on the first re-enabled update, so copy and early-out behavior must be changed together.
- The radial shader derives outward direction from the splat center. This differs from directional wind trails even though both share the wind-deposit ping-pong buffers.
- Buffer resize must refresh the wind-deposit descriptor for both ping-pong pipelines.

## See Also

- [../AGENTS.md](../AGENTS.md) - Collection and controller conventions
- [../WindTrails/AGENTS.md](../WindTrails/AGENTS.md) - Directional wind deposits

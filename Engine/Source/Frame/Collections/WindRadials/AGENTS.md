# /Engine/Source/Frame/Collections/WindRadials/

Client-only stationary wind-deposit splats — each radial deposits an outward impulse into the GPU wind field over a short keyframe lifetime. Spawned only by [Explosions](../Explosions/AGENTS.md), which also registers the controller type (`Register()` here is an empty stub).

## Unique Aspects

- Custom 2-scalar controller keyframes (intensity + size, no position track) hold normalized multipliers scaled by per-instance base magnitudes passed to `AddControlled`, which writes base × keyframe[0] immediately so the radial renders correctly on its spawn frame
- Fire-and-forget with no IDs (`kIdToIndex` unused); PostRender has zero SOA members and exists only to keep the paired `iCount` in lockstep — `Destroy` (controller-expiry reaping) is its only phase with logic
- `AllocateAndCopy` memcpys only the controller arrays; position/intensity/size are derived data fully rewritten by `Update` each frame (position passed through from the previous frame). When the wind setting is disabled, `Update` and render early-out but `Destroy` still runs so rows expire on schedule — a row alive across a disable→enable toggle re-reads stale previous-frame data on the first re-enabled `Update`; know this before changing the copy/skip behavior
- Each radial renders one axis-aligned quad (visibility-culled, projected to base height); per-quad `params.w = 1.0` is the radial flag — `WindDeposit.frag` derives an outward per-fragment direction from the quad center instead of using the CPU-supplied direction trail quads carry
- Shares the A/B ping-pong wind-deposit indirect-count pattern with [WindTrails](../WindTrails/AGENTS.md); on quad-buffer resize `BeginRender` rewrites the SSBO descriptor on both pipelines

## See Also
- `../AGENTS.md` - Collection framework, Controller pattern, three-phase render

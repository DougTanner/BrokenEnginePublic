# /Engine/Source/Frame/Collections/WindRadials/

Client-only stationary wind-deposit splats; controller animates intensity and size while position stays fixed.

## Unique Aspects

- Custom controller keyframes carry intensity + size (no position track); radials are stationary, so Interpolate animates only those two scalars and copies position straight through from the previous frame
- Each radial renders one axis-aligned quad (visibility-culled, projected to base height) on the shared wind-deposit pipeline; per-quad `params.w = 1.0` is the radial flag that distinguishes it from trail quads in `WindDeposit.frag`
- A/B ping-pong wind-deposit pipelines share one dynamic quad buffer; indirect draw count written only to the side matching the active wind texture index, the other receives 0
- Interpolate Update and all render phases early-out when the wind setting is disabled
- Only `Destroy` carries logic (expiry-driven removal); other PostRender phases are empty stubs

## See Also
- [../CLAUDE.md](../CLAUDE.md) - Collection framework, Controller pattern, three-phase render

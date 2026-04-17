## Unique Aspects

- Custom controller type (intensity, size) instead of default; radials are stationary so Update animates only magnitude, not position
- Ping-pong dual pipeline sharing one main buffer; indirect draw count written to only the pipeline matching the current wind-texture index, driving alternating-frame accumulation
- Emits axis-aligned quads with a radial flag in `params.w` to distinguish from trail quads on the shared deposit pipeline; positions projected to base height and visibility-culled
- All render phases and Update early-out when the wind UI toggle is off
- PostRender is a lifetime-only shell (empty phase methods); only Destroy runs

## See Also
- [../CLAUDE.md](../CLAUDE.md) - Collection framework, Controller pattern, three-phase render

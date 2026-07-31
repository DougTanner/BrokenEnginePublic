# Smoke Shaders - Sparse Camera-Following Simulation

Smoke is a client-only 2D density field over a camera-following world rectangle. Objects deposit into the field, two compute passes advect and diffuse it, and world shaders sample the result through shared smoke helpers.

## Frame and Remap Contract

Each frame runs pass B, pass A, then deposit. Pass B reads the previous frame's pass-A output in previous-area coordinates without remapping. Pass A performs the single previous-area-to-current-area remap. Fresh deposits then enter pass A's output so consumers see them in the same frame.

The asymmetric remap requires matching occupancy dilation: B uses direct tile lookup, while A remaps output-tile centers into the previous grid before lookup. Both variants union input activity with existing output occupancy so texels left behind by camera movement are revisited and cleared.

## Sparse Dispatch Invariants

- Occupancy is bit-packed by tile and compacted into an indirect active-tile list. Spread cost scales with occupied coverage.
- Outputs must converge to exact zero after storage-format quantization. Decay and threshold choices must not create positive steady states — values the simulation settles toward instead of reaching zero; occupancy is rebuilt from nonzero output, so residual values keep tiles active forever.
- The two spread passes are not interchangeable. Both call the shared `SmokeSpread` helper, with different noise scales, but only pass B adds the terms that kill off faint smoke: extra decay over raised terrain, a fade toward the edge of the simulation area, and the final step-down walk with threshold zeroing that satisfies the exact-zero rule above. Copying those terms into pass A for symmetry doubles the extinction rate and visibly shortens every plume; dropping them from pass B leaves every touched tile occupied forever and quietly turns the sparse dispatch back into a full-texture one.
- Enable, disable, and recreation use indirect fullscreen clears. Texture occupancy then drains through the following spread frame.
- Wind is sampled in its own area. The UV conversion deliberately negates rescaled X while inverted UV Y cancels the world-to-texture Y sign; changing one sign independently reverses advection.

Wind (`../Wind/AGENTS.md`) owns the velocity field and consumes smoke's populated world-area uniforms.

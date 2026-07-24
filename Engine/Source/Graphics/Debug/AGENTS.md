# Debug - Render-Thread Visualization

Client-only wireframe boxes, spheres, circles, and lines for development overlays. Bodies compile to no-ops when game debug rendering is disabled, and a runtime toggle suppresses submissions by default. The complete primitive set is intentional so temporary debug submissions do not require resource or pipeline additions.

## Lifecycle

- Submission state is file-static and unsynchronized; mutate it only from the render/main thread.
- Storage grows lazily under allocation-tracking suppression. Growth rebinds the affected storage descriptor; steady-state frames update mapped data only, preserving the renderer's record-once command buffers.
- `BeginRender` uploads queued instances after all debug submissions. `EndRender` writes every indirect instance count, including zero, then clears CPU counts. The zero writes prevent prior-frame ghost draws, including when no coord is renderable.
- Pipelines and unit meshes are created by the graphics managers and recorded with the main pass. Transform packing must remain consistent with the debug shaders (`../../../Data/Shaders/Debug/AGENTS.md`); circles rebuild their camera-facing basis in the vertex shader.

## See Also

- Managers (`../Managers/AGENTS.md`) - Buffer and pipeline ownership
- Debug shaders (`../../../Data/Shaders/Debug/AGENTS.md`) - Primitive transform interpretation

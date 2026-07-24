# Debug Shaders - Wireframe Visualization

These shaders render the `DebugRender` wireframe boxes, spheres, circles, and lines. Boxes, spheres, and lines share a world-space path; circles discard instance orientation and rebuild a camera-facing basis while preserving center and uniform scale.

For circle billboards, switch the basis reference from +Z to +X when the view is nearly vertical. The top-down camera makes the usual +Z cross product degenerate, so removing this fallback can produce invalid geometry.

The C++ Debug documentation (`../../../Source/Graphics/Debug/AGENTS.md`) owns primitive submission, buffers, and lifetime.

# Wind Shaders - Sparse Velocity Simulation

Wind is a client-only 2D world-space velocity field that ping-pongs between two textures and drives [Smoke](../Smoke/AGENTS.md). Objects deposit directional or radial velocity; active tiles spread once per frame through hierarchical indirect dispatch.

## Sparse Dispatch Invariants

- Each texture has matching occupancy storage. Each spread variant reads the other texture's occupancy, while dilation writes the active list for its own output variant. Crossing those indices produces stale dispatch coverage.
- Camera movement and zoom require world-position remapping. Dilation remaps tile centers into the previous grid, and both spread variants remap samples through the previous area.
- Occupancy is rebuilt from nonzero output, so decay must reach exact zero. Residual values prevent active tiles from retiring and turn sparse work into persistent work.
- Stored velocity is world-oriented with +Y north. Every conversion between world velocity and texture UV flips Y, including advection and radial deposits.

Smoke populates current and previous area uniforms before wind reads them. Record-once command buffers submit both ping-pong variants; runtime texture selection, occupancy, and GPU-written active lists determine which work executes.

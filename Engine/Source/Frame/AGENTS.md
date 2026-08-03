# Frame - Base Simulation State

## Overview

Engine-owned frame state, fixed-tick phase orchestration, per-cell static data, collision systems, terrain sampling, navigation, and generic collections. Game-specific phase extensions live in the project Frame (`../../../Projects/BrokenEngineSandbox/Source/Frame/AGENTS.md`).

Update Frame Update Pipeline (`../../../Documents/Architecture/FrameUpdatePipeline.md`) when phase ordering or participation changes.

## Architecture

- `FrameInterpolateBase` owns continuous state used for fixed-tick interpolation; `FramePostRenderBase` owns committed deterministic state and per-tick queues. Fixed-tick Interpolate state may feed `FramePostRenderBase` and shared CRC state; shared PostRender/CRC state must not derive from a client render-interpolated frame or client-only collection member. Stop when a proposed source crosses or obscures this boundary.
- Static cell data is derived from the grid coordinate and packed island assets. Elevation is shared; navigation data is server-built and sent to clients. Navigation implementation spans `NavBuild.cpp` (template contour bake), `NavCellData.cpp` (cell merge, visibility, acceleration, serialization), and `NavQuery.cpp` (runtime queries).
- Collision and area-damage queues are thread-local because coord ticks may run in parallel. Their `thread_local` containers must be default-constructed empty: those constructors run during `mi_process_init` (the mimalloc allocator's own process-startup step), before the allocator is ready, so pre-allocating at construction crashes at process start. Size them on first use instead, wrap that resize and any later data-dependent growth in `ScopedSuppressAllocationTracking`, and on overflow `DEBUG_BREAK` naming the pre-allocate constant to raise. The one exception is the collision layer count, which is fixed at compile time, so overflow there logs `kError` and asserts rather than growing. The same rules apply to any new thread-local simulation scratch.
- The produce and consume windows are fixed, not merely ordered: area damage is added during the PostCollision phase and queried during the AreaDamage phase, and each queue is cleared at the end of the phase that owns it. Adding from the AreaDamage phase, or querying from Update, silently drops the hit or delivers it a tick late.
- Collision gathers candidate events across every layer pair, then sorts them globally by time of impact with the layer and object indices as tiebreakers so the order is total and reproducible, and commits each accepted event to both sides. An object flagged destroy-on-collide accepts only its earliest event. Optional per-object maximum-time cutoffs are exclusive, which lets callers reserve exact-time ties for terrain and frame-boundary outcomes. Per-pair collision masks must be bi-directional and same-layer collision is unsupported; both are asserted.
- Large terrain/nav allocations are lazy. Do not make frame constructors allocate proportional to world or mesh size.

## Collection and Serialization Invariants

- `Collections()` order is a dependency order, not just a type list. A producer must precede an owned consumer; Explosions before SmokeTrails is the live example. Tuple-size and client/server type checks do not verify this ordering.
- `Write()`/`Read()` walk full `Collections()` and are build-local. Cross-build server snapshots use `ServerRead()` with `ServerCollections()` and shared members. Preserve tuple order, type parity, and member wire order together.
- Collection-level SOA, initialization, ID-map, count, and CRC rules are authoritative in Collections (`Collections/AGENTS.md`).

## Terrain and Navigation

- World cells use deterministic island placement and rotation. The generator caps each cell at 107 placements; static-data deserialization rejects a larger count before resize/publication, and client rendering derives its aggregate placement arena from this cap and the active-cell subscription slots plus one local unconfirmed cell. Keep world/local transforms, the placement bound, and the project's west/east/north/south convention consistent across CPU sampling and shaders.
- Two terrain-sampling paths exist and must not be mixed. Simulation code calls `FrameElevation`/`FrameNormal`, which read only the cell's own `FrameStaticData` elevation grid, never `mCoordFrames`, and never cross into a neighbouring cell — that is what keeps parallel per-cell ticks from racing the tick-time grid build. Render and other non-tick code calls `GlobalElevation`/`GlobalNormal`, which walk `mCoordFrames` and must never run from frame-tick code; both assert on it. Reach for the convenient global entry point from a new sim query and the assert is the good outcome.
- CPU and GPU terrain elevation deliberately disagree below sea level, and this is not a bug to fix. `TerrainElevation.frag` applies a depth-compression curve to undersea samples that no CPU path mirrors; the CPU folds raw max-blended samples from packed height data. Nothing is read back from the GPU — that texture is produced for downstream shaders only. The difference is visual: the simulation never consumes the curved values, so neither side reaches the CRC. Porting the curve into `CellElevation` to make them agree would change CRC'd sim positions for a purely visual effect. Do not assert stronger guarantees about the texture's sampling format here either.
- Client terrain residency keeps the shared heightmap and hull resident but decommits each template's CPU mesh slice whenever it is not being restored. Coordinate texture and mesh restoration or eviction only in the renderer's window after all pending adds and removes have been processed; this visual residency stays outside deterministic CRCs. Renderer teardown invalidates its mesh allocations without cancelling File-owned range-reload state.
- The obstacle visibility graph is built once per cell, in world space, over every obstacle polygon the cell contains; island templates contribute contour geometry only. Building it per template instead yields the disjoint union of per-island graphs, leaving any route that must round one island to reach another unreachable. Because the graph spans the whole cell, an A* miss is an unexpected result to investigate, not a normal query outcome. Disconnection is the usual cause; a found path whose first waypoint coincides with the start position also reports as a miss.
- The build path and the runtime query share one segment-blocked and one point-in-polygon predicate through `NavBuildInternal.h`. Keep them shared: a divergent epsilon or boundary rule lets the graph claim a segment the query rejects, threading paths through obstacles.
- Frame and gameplay geometry uses meters (one meter per engine unit); UV is only the normalized template representation. For navigation contours, union in UV, convert to centered local meters with the template's anisotropic footprint, apply clearance and simplification there, then map back to UV for storage. A scalar offset in UV makes the world-space margin depend on each footprint's axes and size.
- World-space nav polygons wind clockwise, because placing a template mirrors Y; the template's own UV-space contour winds counter-clockwise. Derive orientation-dependent tests such as vertex convexity from measured winding rather than assuming the counter-clockwise convention the placement hulls carry.
- Navigation version changes invalidate derived data and contribute to the game frame version.

## See Also

- Engine collections (`Collections/AGENTS.md`)
- Game frame (`../../../Projects/BrokenEngineSandbox/Source/Frame/AGENTS.md`)

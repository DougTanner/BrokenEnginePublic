# Frame - Base Simulation State

## Overview

Engine-owned frame state, fixed-tick phase orchestration, per-cell static data, collision systems, terrain sampling, navigation, and generic collections. Game-specific phase extensions live in the project Frame (`../../../Projects/BrokenEngineSandbox/Source/Frame/AGENTS.md`).

Update Frame Update Pipeline (`../../../Documents/Architecture/FrameUpdatePipeline.md`) when phase ordering or participation changes.

## Architecture

- `FrameInterpolateBase` owns continuous state used for fixed-tick interpolation; `FramePostRenderBase` owns committed deterministic state and per-tick queues. Fixed-tick Interpolate state may feed `FramePostRenderBase` and shared CRC state; shared PostRender/CRC state must not derive from a client render-interpolated frame or client-only collection member. Stop when a proposed source crosses or obscures this boundary.
- Static cell data is derived from the grid coordinate and packed island assets. Elevation is shared; navigation data is server-built and sent to clients. Navigation implementation spans `NavBuild.cpp`, `NavCellData.cpp`, and `NavQuery.cpp`, sharing predicates through `NavBuildInternal.h`.
- Collision and area-damage queues are thread-local because coord ticks may run in parallel. Per-tick producers and consumers must preserve phase order and clear their queues at the owning boundary.
- Large terrain/nav allocations are lazy. Do not make frame constructors allocate proportional to world or mesh size.

## Collection and Serialization Invariants

- `Collections()` order is a dependency order, not just a type list. A producer must precede an owned consumer; Explosions before SmokeTrails is the live example. Tuple-size and client/server type checks do not verify this ordering.
- `Write()`/`Read()` walk full `Collections()` and are build-local. Cross-build server snapshots use `ServerRead()` with `ServerCollections()` and shared members. Preserve tuple order, type parity, and member wire order together.
- Collection-level SOA, initialization, ID-map, cardinality, and CRC rules are canonical in Collections (`Collections/AGENTS.md`).

## Terrain and Navigation

- World cells use deterministic island placement and rotation. Keep world/local transforms and the project's west/east/north/south convention consistent across CPU sampling and shaders.
- GPU elevation readback uses the renderer's configured linear sampling path; CPU elevation uses packed height data. Do not assert stronger external format guarantees here.
- Client terrain residency keeps the shared heightmap and hull resident but decommits each template's CPU mesh slice whenever it is not being restored. Coordinate texture and mesh restoration or eviction only in the renderer's drained churn window; this visual residency stays outside deterministic CRCs. Renderer teardown invalidates its mesh allocations without cancelling File-owned range-reload state.
- Navigation version changes invalidate derived data and contribute to the game frame version.

## See Also

- Engine collections (`Collections/AGENTS.md`)
- Game frame (`../../../Projects/BrokenEngineSandbox/Source/Frame/AGENTS.md`)

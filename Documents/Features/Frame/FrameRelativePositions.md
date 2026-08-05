# Frame-Relative Positions

Rewritten 2026-07-03 from the 2026-05-25 sketch; all file:line anchors re-verified against branch 2.0.0 head. Anchor on symbol names — line numbers are best-effort.

## Context

All Frame-system positions are world-absolute. IEEE 754 float precision degrades with magnitude:

| Cells from origin | World coord | Epsilon | Meaning |
|---|---|---|---|
| 1 | 900 | 0.0001 | perfect |
| 10 | 9,000 | 0.001 | fine |
| 90 | 81,000 | 0.008 | ~1cm jitter visible |
| 1,000 | 900,000 | 0.06 | broken |

At ~90-140 cells from origin, smooth rendering and physics degrade. Cells are `Frame::kfCellWidth` × `kfCellHeight` = 900×900 (`Frame.h:187-188`), with a centered base area: `kfBaseAreaMinX/MaxX = ∓450`, `kfBaseAreaMinY/MaxY = ∓450` (`Frame.h:195-198`). `vecArea` lane convention: x=minX, y=maxY, z=maxX, w=minY.

## Goal

Store all collection positions frame-relative (local to the owning cell). `GridCoord` integers carry "where in the world" with zero precision loss; local floats stay within ±450 → ~0.0001 epsilon everywhere, at any distance from origin. The client render path rebases all positions into **camera-frame-relative** space once per render frame, so the GPU, camera, culling, and audio all operate on small floats.

## Decided Design (resolved by code analysis 2026-07-03)

**D1 — Centered local convention.** Local position = world − `FrameOrigin(coord)` where `FrameOrigin(coord) = (coord.x * kfCellWidth, coord.y * kfCellHeight, 0, 0)` (W=0, offset semantics). Local range is the existing base area **−450..+450**, so:
- Cell (0,0) positions are bit-identical to today (its origin is zero) — useful for A/B sanity.
- Frame center is exactly `(0, 0)` — center math simplifies to constants.
- `ComputeFrameBounds` / `InsideArea` checks use a compile-time constant local area equal to the base-area constants; no per-cell data needed.
- `FrameOrigin` products are exact floats for any plausible coord (integer × 900 is exact up to 2^23/900 ≈ 9,300 cells; beyond that the origin itself quantizes but local precision is unaffected — origin only feeds render/transfer offsets where neighboring-cell *differences* stay exact).

**D2 — Terrain queries need no call-site changes.** All sim-side terrain queries already route through `IslandTerrain::FrameElevation` / `FrameNormal` / `MakeFrameElevationSampler` (the Frame Purity refactor landed since the original sketch; `GlobalElevation`/`GlobalNormal` are render-only, asserted out of the tick). `MakeFrameElevationSampler` (`IslandTerrain.cpp:439-458`) currently computes `fCellOriginX = kfBaseAreaMinX + coord.x * kfCellWidth` and `Sample` does `fLocal = pos − fCellOrigin`. With centered local positions the coord term disappears: `fCellOriginX/Y` become the constants `kfBaseAreaMinX/Y`. One function changes; the ~13 `FrameElevation`/`FrameNormal` call sites (list below) and both `TerrainUtils.cpp` helpers are untouched. Precision inside the sampler *improves* (no large-magnitude subtraction).

**D3 — Render rebase at the interpolate-copy, not per renderer.** `GameBase.cpp:417-456` already builds a render-only copy per active coord (`AllocateAndCopy` + `FrameInterpolate::Update` into `mRenderInterpolates`). Insert one step after `Update`: `FrameInterpolate::OffsetPositions(rInterpolate, vecOffset)` with `vecOffset = FrameOrigin(coord) − FrameOrigin(cameraCoord)` (skip when zero, i.e. the camera frame). Everything downstream of `mRenderInterpolates` — collection `Render()` methods, `gpCamera->Update`, visibility culling, the audio Sounds path, UI reads — then works unchanged in one consistent camera-frame-relative space. No merge infrastructure; the old sketch's `MergeFramesForRender` concept is dead.
- `OffsetPositions` follows the two-tier dispatch pattern (`FrameCollections.h` tuples + Players explicit): per collection, add offset to every world-space-position array (`pVecPositions`, debug nav waypoints, island destinations used by debug render, Targets positions, engine Sounds/Explosions positions). Directions/velocities/normals (W=0) are NOT offset.
- Mutates only the render copy — never CRC'd sim state.

**D4 — Transfer conversion at the four builders.** `dest_local = src_local − delta * cellDims` (one `XMVectorNegativeMultiplySubtract`), applied where the `TransferRequest` is built and `ComputeTransferDelta`'s result is in hand — `PlayersNavigation.cpp:83`, `Blasters.cpp:239`, `Missiles.cpp:377`, `Spaceships.cpp:421`. `TransferData.vecPosition` is then dest-local from birth; `game::SpawnTransfer` (`SpawnTransfer.cpp:12`), server `ServerTransferManager::SpawnTransfers` (`ServerTransferManager.cpp:122-144`), and the client replay path (`ReconcileReplayTick.cpp:134`) all consume it with **zero conversion** — client/server symmetric by construction. Builders run identically on both sides (only the server harvests, but the deterministic stream is identical either way).

**D5 — Version bump.** Positions serialize as raw floats whose meaning changes → bump the `Frame::kiVersion` base constant (`Frame.cpp:11`) by +1 **on top of** the completed 115→116 version-gate bump (this plan takes the result +1). Network full-state, saves, and replays are all gated by the same constant — no other serialization change needed. `FrameStaticData` serialization (`vecArea`, placement `f2WorldPos`) is untouched — those stay world-space.

## Work Items

### 1. Coordinate infrastructure (game `Frame.h`)

- Add `inline XMVECTOR XM_CALLCONV FrameOrigin(engine::GridCoord coord)` — `(coord.x * kfCellWidth, coord.y * kfCellHeight, 0, 0)`. This is the single conversion primitive; `ToWorld`/`ToLocal` are just `XMVectorAdd`/`Subtract` with it (add thin inline wrappers only if call sites read better with them).
- Add `inline constexpr` local-bounds constants: `kVecLocalArea` (XMVECTOR in vecArea lane order from the `kfBaseArea*` constants — note `XMVECTOR` cannot be `constexpr`; use `XMVectorSet` in an inline function or an `XMVECTORF32`) and a `FrameBounds kLocalBounds {−450, −450, 450, 450}` equivalent built from the same constants.
- `ComputeFrameBounds` / `IsOutOfBounds` / `ComputeTransferDelta` (`Frame.h:102-139`) keep their shapes; callers switch the input from `rStaticData.vecArea` to the local constant (§4).
- `ComputeFrameArea` (world-space, `Frame.h:114-121`) stays — `vecArea` remains the world/render/subscription-domain description of a cell.

### 2. Terrain (engine `IslandTerrain`)

- `MakeFrameElevationSampler` (`IslandTerrain.cpp:439-458`): `fCellOriginX/Y = kfBaseAreaMinX/Y` (constants; drop the coord terms). Sampler comment updated: positions are cell-local.
- `FrameElevation` / `FrameNormal` / `FrameElevationSampler::Sample`: no changes (they inherit the fix).
- `GlobalElevation` / `GlobalNormal` / `CoordFromPosition` (render path): unchanged — still world-in. Render-side callers convert (§6).
- Elevation grid build (`BlendPlacementIntoGrid` path) is derived-data construction from world-space placements into a cell-indexed grid — unchanged.
- **NavData becomes cell-local at build.** `NavQueryDirection(vecPosition, rVecIslandDestination, rStaticData.navData, ...)` (`PlayersNavigation.cpp:280`) compares sim positions against nav nodes; nodes derive from world-space island placements. Subtract the cell origin during NavData construction (server-side build; clients receive it over the wire — the wire payload becomes cell-local too, which is fine because NavData is per-cell and never compared cross-cell). Audit `NavData`/`NavContour` node fields and `engine::NavQueryDirection` internals during execution; `kiNavDataVersion` bump if the wire format's meaning changing matters independently of `Frame::kiVersion` (it is a term of it — one bump covers both).

### 3. Transfers

- Four builders apply `dest_local = src_local − delta * cellDims` when storing `TransferRequest.data.vecPosition` (D4 sites).
- `ServerTransferManager` (`CollectTransfers`/`SortTransfersByType`/`SpawnTransfers`, `ServerTransferManager.cpp:50-144`): no position changes. Its error-log at `:67` prints `rRequest.data.vecPosition` — now dest-local; fine (log-only).
- `SpawnTransfer.cpp`, `ReconcileReplayTick.cpp`: no changes.
- `common::ValidateVector<IS_POSITION>` calls at spawn/transfer sites: unchanged (W-lane checks are space-agnostic).

### 4. Bounds checks and center math (switch to local constants)

`ComputeFrameBounds(rStaticData.vecArea)` → `kLocalBounds` (or `ComputeFrameBounds(kVecLocalArea)`):
- `PlayersNavigation.cpp:66`
- `PlayersCombat.cpp:255`
- `SpaceshipsCombat.cpp:127`
- `Spaceships.cpp:405`
- `MissilesUpdate.cpp:280`
- `Missiles.cpp:361`
- `BlastersUpdate.cpp:265`
- `Blasters.cpp:223`

Other sim-side `vecArea` reads:
- `FrameTick.cpp:76` — `Collision::Collide(alignments, rStaticData.vecArea)` → pass `kVecLocalArea`. `Collision::SetupZones` (`Collision.cpp:226`) is space-agnostic (maps positions into a zone grid over whatever area it is given) — param only, no body change.
- `Frame.cpp:212` — `common::InsideArea(vecPosition, rStaticData.vecArea)` → `kVecLocalArea`.
- `Frame.cpp:237-241` (+ `:249, :274-275` and the rest of the `SpawnSpaceshipGroup` grid rasterizer) — replace the `f4Area` world extraction with the base-area constants; grid anchors/pitch become cell-local.
- `Players.cpp:352-353` — spawn center → `(0, 0)` (frame center is origin under D1).
- `Players.cpp:708-711` — nav-destination frame center → `XMVectorSet(0, 0, gBaseHeight, 1)`.
- `PlayersNavigation.cpp` mode-4/5 island destination from placements — placement `f2WorldPos` is world-space: `local = placementPos − FrameOrigin(rStaticData.coord)` at the destination-selection site (the RNG draw structure is untouched — see the Players AGENTS.md draw-count invariant; this only changes the value stored in `pVecIslandDestinations`).
- `Game.cpp:164-165` — frame-center-from-vecArea helper: identify its consumer at execution; if it feeds camera/UI it keeps world space (or gains `FrameOrigin`), if it seeds sim state it becomes `(0,0)`-based.

Sim sites that need **no change** (same-frame relative math): all cross-collection targeting/homing (`GetMissileTarget` `Frame.cpp:459-466`, spaceship→player scans), collision layers, `FrameInterpolateBase::IsVisible`, Pushers arena centering (`PushersUpdate.cpp:60-63`), position integration (`pos += vel * dt`), blaster/missile spawn-from-owner offsets, and the engine Sounds sink (`SoundsUpdate.cpp:17` — same-frame copy).

### 5. Render rebase (client, engine `GameBase` + game `FrameInterpolate`)

- `GameBase.cpp:417-456` `interpolateFrame` lambda: after `FrameInterpolate::Update`, apply `OffsetPositions(mRenderInterpolates.at(rCoord), vecOffset)`, `vecOffset = (coord − cameraCoord) * cellDims` computed in integers then converted (exact). Camera coord offset is zero — skip.
- New `FrameInterpolate::OffsetPositions` + per-collection `OffsetPositions` via the `ForEachInterpolate*` tuple dispatch + explicit Players call (two-tier pattern). Per collection enumerate position-semantic arrays only (W=1 positions; debug waypoints; Targets positions; engine Explosions/Sounds positions). Velocities/directions untouched.
- `gpCamera->Update(mRenderInterpolates.at(cameraCoord))` (`GameBase.cpp:466`): input already camera-frame-relative (offset zero) — camera position becomes camera-frame-local **implicitly**. See §7 for continuity.
- Renderers (`MissilesRender.cpp:69-102` pattern, all collections): no changes — positions, `InVisibleArea` culling against `f4RenderVisibleArea`, and `ModelLayout.f4Position` GPU writes all stay internally consistent in the new space.

### 6. Engine render/world-space feeds (client)

Everything the GPU or render path sees must be in the same camera-frame-relative space:
- `MainUniforms.cpp:26` — per-coord `staticData.vecArea` → subtract `FrameOrigin(cameraCoord)` before upload (terrain/island cell placement).
- `GraphicsUtils.cpp:82` `ProjectToBaseHeight` — queries `GlobalElevation` (world-in): add `FrameOrigin(cameraCoord)` to the camera-relative input position before the query. Camera coord is available render-side via `gpGame` (render path is exempt from Frame Purity). Precision note: the world-space query at large coords carries cm-scale error — acceptable for heightmap sampling (texel pitch ~7 units), and render-only.
- **Sweep item**: enumerate all other world-space→GPU feeds during execution — island mesh instance placement, water grid snap (`CameraBase::CalculateMatricesAndVisibleArea` quad-snap uses camera position — consistent once camera is local), `GlobalUniforms` camera/eye uploads, visible-area rectangles (`f4RenderVisibleArea`, `f4LargeVisibleArea`). Each either already flows from now-local camera/entity state (no change) or reads world `vecArea`/placements (subtract `FrameOrigin(cameraCoord)`).

### 7. Camera and subscriptions (client)

- Camera storage (`CameraBase.h:28-30` `mVecPosition`/`mVecEyePosition`) becomes camera-frame-local implicitly via §5. World anchor = `gpGame` client grid coord.
- **Coord-change continuity**: when the flagship transfers, the client coord changes and every offset shifts by one cell while the flagship's local position wraps by `∓cellDims`. Rebase the camera's stored positions by `−delta * cellDims` at the coord change so eased/lerped camera state stays continuous — `Game::SetClientGridCoord()` is the mandated single write point (per `Source/AGENTS.md`) and the natural hook.
- **Atomicity risk (execution-time verify)**: the rebase, the `cameraCoord` used in §5 offsets, and the interpolate snapshot that shows the wrapped position must land on the same rendered frame, else a one-frame 900-unit pop. Verify how `SetClientGridCoord` timing relates to `kiRenderBehindTicks`-delayed interpolates; if they can straddle, derive the render `cameraCoord` from the snapshot generation rather than live client coord.
- Subscription math (`Game.cpp:205-209`, and the active-set/visible-neighbor logic around `Game.cpp:392-437`): camera visible areas are now camera-frame-local; convert to world with `+FrameOrigin(clientCoord)` before intersecting cells' world `vecArea` (or subtract per-cell — either way, a handful of non-CRC client sites).

### 8. Audio (client)

- `PlayOneShot3d` (9 sites: `PlayersCombat.cpp:192, 239, 371, 447`, `Missiles.cpp:496`, `BlastersUpdate.cpp:331`, `SpaceshipsCombat.cpp:57, 158`, `Spaceships.cpp:545`): emitter positions are frame-local; the listener derives from the (now camera-frame-local) camera (`StaticVoices.cpp:509-540`). Signature gains the emitting cell's coord (callers pass `rStaticData.coord` — in scope at all 9 sites); AudioManager stores position+coord in the one-shot queue and applies `+FrameOrigin(coord) − FrameOrigin(cameraCoord)` at dequeue/mix time, outside the tick (keeps `gpGame` reads out of frame-tick code). Same-frame one-shots (the overwhelming majority) get offset zero.
- Persistent Sounds collection: positions rebased by §5's `OffsetPositions` in the render copy; `AudioManager::Update` and the listener both read camera-frame-local — correct with no further change.

### 9. Server GDI monitoring window (server)

The server-only GDI monitor (`Source/Server/`) is the one position consumer with no camera frame to rebase into. Audit its position reads at execution; wherever it plots entities or cells in a world layout, convert per cell with `world = local + FrameOrigin(coord)` (display-only, non-CRC, trivially cheap at monitor refresh rates).

### 10. Serialization / CRC / determinism

- `Frame::kiVersion` base +1 (D5).
- CRC composition, `SharedMembers`/`SharedCrcMembers`, `LogDifferences`, `Write`/`Read`/`ServerRead`: no structural changes — positions are the same floats with new meaning on both sides simultaneously.
- Determinism: all conversions in the CRC'd tick (transfer builder subtraction, island-destination localization, sampler constant) are plain IEEE ops under `/fp:strict`, identical on client and server. The render rebase (§5-8) is entirely outside the CRC.
- Replays/saves from before the change are invalidated by the version gate — intended.

## Execution notes

- Effort is Architectural: during **Implement and propagate**, suggest disjoint slices of (a) sim-side items 1-4 + 9 and (b) client render/camera/audio items 5-8 — disjoint file sets except `Frame.h`.
- The old sketch's `.txt` (deleted by this rewrite) undercounted the spread of breakage and predates the `FrameElevation` purity refactor, the `ServerTransferManager` split, and the Players/Spaceships file splits — do not consult stale copies.

## Out of scope

- Island placement storage (`IslandPlacement::f2WorldPos`), `FrameStaticData::vecArea`, elevation-grid build internals — remain world-space by design.
- `GlobalElevation`/`GlobalNormal` API shape (stay world-in; render callers convert).
- Double-precision or fixed-point world coordinates; camera-origin *sub-cell* rebasing (camera-frame anchor is sufficient at RTS camera heights).
- Debug HUD/log output showing local instead of world coordinates — cosmetic; note occurrences, do not chase.
- Any change to RNG draw structure or tick phase order.
- Removing the world-coordinate `CoordFromPosition` render path.

## Grill Resolutions (user-confirmed 2026-07-03)

All six open decisions were grilled and resolved to the recommended options; the Decided Design and Work Items above are final:

1. **D1**: centered −450..+450 local convention.
2. **D3**: single `OffsetPositions` on the `mRenderInterpolates` copy.
3. **D4**: transfer conversion at the four builders; `TransferData.vecPosition` is dest-local from birth.
4. **NavData**: cell-local at build time.
5. **Camera continuity**: rebase in `Game::SetClientGridCoord()`; execution MUST verify the wrapped-snapshot/coord-change timing against `kiRenderBehindTicks` — if they can straddle a rendered frame, escalate to deriving the render `cameraCoord` from the interpolate snapshot itself (structurally pop-proof) as the in-plan fallback, not a new decision.
6. **PlayOneShot3d**: signature gains the emitting cell's `GridCoord`; conversion at dequeue/mix time in AudioManager, outside the tick.
7. **Closing-question addition**: server GDI monitoring window audited/converted per §9 (adjacent consumer with no camera frame — added as a work item).

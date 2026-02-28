# BrokenEngineSandbox - Sample Game Implementation

A space combat game demonstrating the full capabilities of the Broken Engine. Features fast-paced dogfighting, wave-based enemy spawning, and terrain interaction.

## IMPORTANT: Frame Purity Constraint

Frame code is purely functional. Frame updates must only rely on explicit function parameters -- Frame code must NEVER query Game (`gpGame`) for anything. The Frame does not know which Player is human and which is AI. Human player identity, camera shake, death screen transitions, and respawn orchestration are Game-level responsibilities. Frame code operates on data-driven parameters only (e.g., iterating all players to find alive ones, rather than querying Game for a player index).

## Engine Integration

The game follows the standard engine architecture:
- **Game class** - Inherits from `engine::GameBase`, coordinates all subsystems
- **Frame system** - Game state management, inherits from `engine::FrameBase`
- **Namespace** - All game code is in the `game` namespace
- **Data Pipeline** - Assets processed by DataPacker and loaded via FileManager

## Directory Structure

### `/Frame/` - Game State and Core Systems
Phase-separated frame structures for deterministic replay, player controller, and wave spawning system.
- [Frame/CLAUDE.md](Frame/CLAUDE.md)

### `/Frame/Collections/` - Dynamic Object Management
Blasters, missiles, and spaceships using SOA layout with dynamic memory allocation.
- [Frame/Collections/CLAUDE.md](Frame/Collections/CLAUDE.md)

### `/Graphics/` - Camera and Rendering
Game-specific camera controller with menu animations and player tracking.
- [Graphics/CLAUDE.md](Graphics/CLAUDE.md)

### `/Input/` - Control System
Three-tier input processing with automatic keyboard/mouse and gamepad detection.
- [Input/CLAUDE.md](Input/CLAUDE.md)

### `/Ui/` - User Interface
ImGui-based HUD, menus, and settings screens.
- [Ui/CLAUDE.md](Ui/CLAUDE.md)

### `/Profile/` - Performance Profiling
Game-specific CPU counters and timers extending the engine's ProfileManager.
- [Profile/CLAUDE.md](Profile/CLAUDE.md)

## Game Class (`Game.h/cpp`)

Central game coordinator inheriting from `engine::GameBase`. Accessed via `gpGame` singleton pointer.

**Game Lifecycle**: Manages state transitions between main menu, gameplay, and death screen via `GameFlags`. Supports new game and restart. Handles music playlist switching between menu and gameplay modes with callback-driven track progression (client-only via `#ifdef BT_CLIENT`). Camera ownership is client-only; server builds omit the Camera member entirely. Server builds (`BT_SERVER`) start directly in game mode (`kGame`) with `kNone` UI state, bypassing the main menu.

**Client Networking** (`#ifdef BT_CLIENT`): Integrates with NetworkClient for multiplayer using a rollback-and-replay reconciliation architecture with server-input extrapolation (not client-side prediction). `ConnectToServer()`/`DisconnectFromServer()` manage the NetworkClient lifetime and clear all server state (confirmed state, pending full state, update buffer, transfer changes, last server player inputs) on disconnect. `StartServerDiscovery()` creates a `NetworkDiscoveryScanner` for LAN server auto-detection; `PollNetworkClient()` polls the scanner and auto-connects when a server is found (transitioning to game mode), or cleans up on timeout. `PollNetworkClient()` detects server-initiated disconnects via `WasDisconnected()` and returns to the main menu (transitioning to pause UI state). After those checks, it polls ENet, checks for player assignment (updating human tracking state and clearing the assignment via `ClearAssignment()` to prevent re-processing), applies received full states and establishes them as the confirmed state (serializing all current frames, saving human tracking state, and pruning stale updates from the buffer), and buffers received delta updates into `mServerUpdateBuffer` keyed by frame number (skipping frames at or before the confirmed state). `ApplyReceivedFullStates()` has two distinct paths: on initial connection (no confirmed state yet), it moves received frames directly into `mCurrentFrames`, ensures next frames exist, and hydrates client-only objects (`HydrateClientObjects()`); on subscription updates (confirmed state already exists), it hydrates client-only objects, serializes the frames, and stores them in `mPendingFullState` for deferred injection during `Reconcile()` (avoiding mid-replay mutation of `mCurrentFrames`). `ApplyReceivedUpdates()` buffers server updates into a sorted map for sequential replay processing. `SendNetworkInput()` sends the locally captured `mLocalPlayerInput` to the server. `ChangeFrame()` resets the discovery scanner before disconnecting.

**Rollback and Replay** (`Reconcile()`): Called each client tick before physics. First checks for a gap between confirmed state and the first buffered frame. If a gap exists and `mPendingFullState` is available, performs a gap fallback: restores `mCurrentFrames` from the confirmed state (discarding extrapolated state), injects pending coords, restores human tracking state and simulation counters, then replays all missing frames (from confirmed+1 through bufferFirst-1) using the full physics pipeline with extrapolated inputs from `mLastServerPlayerInputs`, establishes a new confirmed state at `bufferFirst - 1`, prunes the buffer, and returns (next tick replays normally from the new confirmed state). Otherwise, restores all frames to the last confirmed state (via serialized snapshots), handles race conditions where pending full state arrived after Reconcile already replayed past its frame (injecting those coords immediately), then replays consecutive server updates from `mServerUpdateBuffer` by re-running the full frame update pipeline (active islands update, Interpolate, PostRender, collision, transfer, destroy, spawn) for each buffered frame. `BuildFrameInputForFrame()` constructs per-coordinate FrameInputs from a specific server update, separating transfer StatusChanges into `mServerTransferStatusChanges` and applying server player inputs. During replay, injects `mPendingFullState` coords at the matching transfer frame to synchronize new cell data with the replay timeline. After each replayed frame, validates CRCs against server-reported values; on mismatch sends a desync report, triggers `DEBUG_BREAK()`, and disconnects. After all consecutive frames are replayed, saves the last replayed frame's `mFrameInputs` player inputs into `mLastServerPlayerInputs` for client-side extrapolation, prunes stale coords outside the current human position's active set from `mCurrentFrames`, saves the result as the new confirmed state, and clears stale pending full states that have been replayed past. The `ConfirmedState` struct stores the frame number, simulation time, serialized frame data per grid coordinate, and human tracking state (grid coord, player ID, armor). The `PendingFullState` struct stores a frame number and serialized frame data per grid coordinate for deferred subscription update injection.

In network mode, `BuildFrameInputs` uses a separate code path that applies last server-confirmed player inputs from `mLastServerPlayerInputs` (extrapolation) rather than injecting local raw input into physics frames. Local input is still captured separately into `mLocalPlayerInput` for `SendNetworkInput`. This bypasses the offline `BuildFrameInput` helper and AI wingmen logic.

**Server Networking Orchestration** (`#ifdef BT_SERVER`): Eight server methods integrate with NetworkServer to manage multiplayer: `HandleNewClientsServer()` queues unassigned clients for spawn; `ProcessSpawnRequestsServer()` converts network spawn/respawn requests; `ComputeActiveSetServer()` unions all clients' active coordinates; `BuildFrameInputsServer()` maps network inputs to human player indices and saves spawn StatusChanges into `mBroadcastSpawns` (transfers handled separately in `HarvestTransfersServer()`), and clears `mBroadcastSpawns`/`mBroadcastTransfers` at the start of each call to prevent stale data from prior physics frames; `HarvestTransfersServer()` tracks cross-cell entity transfers into `mBroadcastTransfers` and queues subscription updates with the transferred player's newly-spawned ID in the destination cell; `FinalizeNewClientsServer()` assigns spawned players to clients, ensures frames exist at all `activeCoords` for server simulation, and sends full state for only `pendingFullStateCoords` (coords the client doesn't already have); `HandleSubscriptionUpdatesServer()` sends player assignment (with the new player ID from the destination cell) via `SendAssignPlayer()` which computes `pendingFullStateCoords`, then ensures frames exist and sends full state via `SendFullState()` for only those newly-subscribed coords after grid transfers; `BroadcastStatusChangesServer()` buffers unfiltered data (spawns, transfers, and player inputs from `mFrameInputs`) to the ring buffer via `BufferFrame()`, then sends the same unfiltered data to each client (filtered only to their active coords) via `SendUpdate()`. The pre-physics methods (poll/handle/spawn) are called from Main.cpp's server loop, while the post-physics methods (finalize/subscribe/broadcast) are called per-physics-frame inside `UpdateFramesOnly()` to ensure clients receive updates for every simulated frame.

**Human Player Tracking**: Identifies the human player by stable `player_t` ID rather than array index (indices shift due to swap-and-pop removal). Detects human death to trigger the death screen and monitors armor changes for camera shake. Tracks the human's grid coordinate across multi-frame transfers.

**Multi-Frame Grid Orchestration**: Manages the active set of grid coordinates and builds per-coordinate `FrameInput`. In client builds, the active set is the human's cell plus neighbors (with origin always active), and `BuildFrameInputs` maps human input to the human's cell and drives AI wingmen via `PlayerAi`. In server builds, only human players exist -- `ComputeActiveSetServer()` computes the active set as the union of all connected clients' active coordinates, and `BuildFrameInputsServer()` maps each client's network input to their human player's index via `NetworkServer::DrainPendingInputs()` and buffers StatusChanges for broadcasting. Creates frames at missing coordinates, ensures destination frames exist for buffer swaps, and garbage-collects frames outside the active set.

**Transfer Harvesting**: After carry-forward, reads transfer requests from active frames and spawns entities into destination cells via a centralized `SpawnTransfer()` helper. In client builds, `HarvestTransfers()` uses two distinct paths: during reconciliation (when server transfer data is available in `mServerTransferStatusChanges`), it sorts transfers by `uiSequence` to restore original spawn order (which is lost during network serialization's type-grouping), then applies all transfers from the server and tracks human player grid migration from local `transferRequests` without spawning (the server already spawned entities); during extrapolation or offline play, it performs local transfer harvesting from `transferRequests`, records transfers into the human's cell for replay determinism via `mPendingTransferChanges`, and tracks human player grid coordinate migration. In server builds, `HarvestTransfersServer()` assigns sequential `uiSequence` numbers to each coordinate's transfers for deterministic ordering across the network, records transfers into `mBroadcastTransfers`, and detects human player grid transfers across all connected clients, capturing the newly-spawned player ID from the destination cell and queuing `SubscriptionUpdate` entries for `HandleSubscriptionUpdatesServer()` to send player assignment (with the new ID) and full-state updates for newly-visible cells. During replay playback, skips human-cell transfers (handled by `ApplyTransferStatusChanges()` which processes transfer entries from the recorded stream and removes them before checksum validation).

**Spawn Orchestration**: In client builds, buffers spawn and respawn status changes that are drained by GameBase only when physics steps will run, preventing event loss on frames with zero full updates. Uses `SpawnFlags_t` (flags-based via `common::Flags<SpawnFlags>`) to track waiting-for-human-spawn and respawn-requested states. On respawn, resets the human's grid coordinate to origin. In server builds, `ProcessSpawnRequestsServer()` converts network spawn/respawn requests into `ClientSpawnInfo` entries, `BuildFrameInputsServer()` injects spawn StatusChanges for waiting clients and takes a pre-spawn player ID snapshot, and `FinalizeNewClientsServer()` matches newly-spawned players to waiting clients by diffing post-spawn IDs against the snapshot, then sends player assignment and full state, and refreshes the snapshot so subsequent physics frames in the same tick can correctly diff new spawns. `HandleNewClientsServer()` queues clients without assigned players for initial spawn. `Reset()` consolidates all human tracking state cleanup (player ID, spawn flags, armor, grid coordinate, pending changes).

**Persistence**: Quicksave via grid serialization (all frames plus human grid coordinate). Sound settings persisted separately. `ReplayMeta` struct captures human tracking state (grid coordinate, player ID, armor) and is persisted to a separate `.replay.meta` file for deterministic replay restore via `RestoreReplayMeta()`.

**PlayerAi** (`PlayerAi.h/cpp`): Drives AI wingmen with gradient-based terrain contour following and targeted burst fire. Client-only; called per AI player during `BuildFrameInput()`. Server builds have no AI wingmen -- only human players exist in multiplayer.

## Configuration Files

| File | Purpose |
|------|---------|
| `Pch.h` | Compile-time feature toggles: vcxproj defines either `BT_CLIENT` or `BT_SERVER` explicitly. `kbSingleInstance` is true for server builds. `inline constexpr bool` toggles with `if constexpr` for zero-overhead conditional compilation. Includes `Common.h` and `Engine.h` aggregation headers |
| `Frame/HealthDamage.h` | Combat balance values and collision category/mask configuration |
| `Profile/GameProfile.h` | Performance profiling zones |
| `Version.h` | Save file version tracking |

## Build Configuration

The vcxproj uses `/fp:strict` for deterministic floating-point math across different hardware configurations, complementing the engine's SSE4-only DirectXMath configuration and FMA3 disable.

## See Also
- Engine Architecture: [../../../Engine/Source/CLAUDE.md](../../../Engine/Source/CLAUDE.md)
- Common Utilities: [../../../Common/CLAUDE.md](../../../Common/CLAUDE.md)

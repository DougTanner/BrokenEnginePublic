# Architecture: Frame Update Pipeline

> Maintained architecture reference for `Engine/Source/Frame/`, `Engine/Source/Main.cpp`, and the game frame pipeline; update when depicted phase ordering, main-loop sequencing, frame lifecycle, or collection participation changes.

## RunFrameTick Pipeline

All physics phases are unified into a single `RunFrameTick()` function (defined in `FrameTick.cpp`). Both `GameBase::ServerUpdate()` and client reconciliation replay/catch-up call the same function. Each Frame runs all five phases sequentially; multiple Frames are dispatched in parallel via `Dispatch()`.

```mermaid
%%{init: {'theme': 'default'}}%%
flowchart LR
    classDef interpolate fill:#dbeafe,stroke:#3b82f6
    classDef postrender fill:#fef3c7,stroke:#d97706
    classDef collision fill:#fee2e2,stroke:#ef4444
    classDef lifecycle fill:#d1fae5,stroke:#059669

    subgraph RunFrameTick ["RunFrameTick (per-Frame)"]
        subgraph Phase1 ["Phase 1: Interpolate"]
            i_alloc["AllocateAndCopy"]:::interpolate
            i_update["Update"]:::interpolate
        end

        subgraph Phase2 ["Phase 2: PostRender"]
            pr_alloc["AllocateAndCopy"]:::postrender
            pr_update["Update"]:::postrender
        end

        subgraph Phase3 ["Phase 3: Collision"]
            pr_precol["PreCollision"]:::collision
            collide["Collision::Collide"]:::collision
            pr_postcol["PostCollision"]:::collision
            pr_area["AreaDamage"]:::collision
        end

        subgraph Phase4 ["Phase 4: Transfer"]
            pr_transfer["Transfer"]:::lifecycle
        end

        subgraph Phase5 ["Phase 5: Destroy/Spawn"]
            pr_destroy["Destroy"]:::lifecycle
            pr_spawn["Spawn"]:::lifecycle
        end
    end

    i_alloc --> i_update
    i_update --> pr_alloc
    pr_alloc --> pr_update --> pr_precol --> collide --> pr_postcol --> pr_area --> pr_transfer --> pr_destroy --> pr_spawn
```

## Client Main Loop

Main.cpp calls `ProcessInput()`, `ClientUpdate()`, `Render()`, and `AudioManager::Update()` in sequence. The client has no per-tick physics loop — `ClientSession::Reconcile()` is a single-pass call that validates, replays mismatches, and forward-sims to the post-advance tick target.

```mermaid
%%{init: {'theme': 'default'}}%%
flowchart TD
    classDef input fill:#e0f2fe,stroke:#0284c7
    classDef physics fill:#fef3c7,stroke:#d97706
    classDef network fill:#fee2e2,stroke:#ef4444
    classDef render fill:#d1fae5,stroke:#059669
    classDef agent fill:#ede9fe,stroke:#7c3aed

    start(["Main Loop Start"])

    msgs["ProcessMessages()"]:::input

    agent_drain["AgentCommandServer::Drain()<br/>(dev, if --agent-port)"]:::agent

    preupdate["ProcessInput()"]:::input

    subgraph tick_frames ["GameBase::ClientUpdate()"]
        poll["ClientSession::Poll()"]:::network
        stall_check{"IsStalled?"}:::network
        poll --> stall_check

        ts["TimeStep::TickRealtime()<br/>compute iFullTicks"]:::physics
        subs["ClientSession::UpdateDesiredCoords()<br/>+ UpdateSubscriptions()<br/>unsubscribe stale, rebuild, subscribe"]:::network
        prepare_active["PrepareActiveSet()"]:::physics
        clamp["Clamp iFullTicks to<br/>runtime clock-state ceiling<br/>(latest tick - target behind + slack)<br/>(AbsorbUnusedTicks)"]:::network
        advance["Advance miTickCounter +<br/>mfCurrentTime"]:::physics
        reconcile["ClientSession::Reconcile()<br/>single pass: drop validated,<br/>replay mismatches, forward sim<br/>to target tick"]:::network

        stall_check -->|No| ts --> subs --> prepare_active --> clamp --> advance --> reconcile
    end

    subgraph render_method ["GameBase::Render()"]
        r_interp["FrameInterpolate::<br/>AllocateAndCopy + Update"]:::render
        r_camera["Camera::Update()<br/>(current render frame; only when<br/>bHaveRenderableCamera)"]:::render
        r_swapchain{"Swapchain deferred<br/>or frame poisoned?"}:::render
        r_skip["Recreate / retry swapchain<br/>skip render + present"]:::render
        r_islands["Game::UpdateActiveIslands()<br/>pack island instances"]:::render
        r_global["RenderGlobal()"]:::render
        r_render["RenderMainPresentAcquire()<br/>BeginRender / Render / EndRender<br/>main + present"]:::render

        r_interp --> r_camera --> r_swapchain
        r_swapchain -->|Yes| r_skip
        r_swapchain -->|No| r_islands --> r_global --> r_render
    end

    audio["AudioManager::Update()"]:::render

    start --> msgs --> agent_drain --> preupdate --> tick_frames
    tick_frames --> r_interp
    r_skip --> audio
    r_render --> audio
    audio -->|next iteration| start
```

## Server Main Loop

Main.cpp calls `ServerUpdate()` then `ServerUpdateDisplayStats()`. Network orchestration is encapsulated within `GameBase::ServerUpdate()`, which delegates phase operations through the runtime owned by `ServerSession`.

```mermaid
%%{init: {'theme': 'default'}}%%
flowchart TD
    classDef physics fill:#fef3c7,stroke:#d97706
    classDef network fill:#fee2e2,stroke:#ef4444
    classDef server fill:#f3e8ff,stroke:#9333ea
    classDef agent fill:#ede9fe,stroke:#7c3aed

    start(["Server Loop Start"])

    msgs["ProcessMessages()"]:::server

    subgraph tick_frames ["GameBase::ServerUpdate()"]
        pre_tick["ServerSessionRuntime::Poll()"]:::network
        agent_drain["AgentCommandServer::Drain()<br/>(dev, if --agent-port)"]:::agent

        save_load_replay["GameSaveLoad::SaveLoadReplay()"]:::server

        wait_tick["ServerSessionRuntime::WaitForTick()"]:::server

        ts["TimeStep::TickRealtime()<br/>compute iFullTicks"]:::physics
        prepare_active["PrepareActiveSet()"]:::physics

        subgraph physics_loop ["Fixed Timestep Loop (32 Hz)"]
            prepare_tick["ServerSession::PrepareTick()"]:::network
            sync_replay["GameSaveLoad::SyncReplayTick()<br/>(recording/replaying/record-pending only)"]:::server
            replay_terminal{"Final replay reader retired?"}:::server
            replay_reload["Reload replay loop<br/>(skip dispatch/finalize)"]:::server
            dispatch_s["Dispatch RunFrameTick()"]:::physics
            harvest["HarvestTransfers()"]:::physics
            frame_swap["SwapFrames()"]:::physics
            broadcast_tick["ServerSessionRuntime::CompleteTick()"]:::network
            prepare_tick --> sync_replay --> replay_terminal
            replay_terminal -->|No| dispatch_s --> harvest --> frame_swap
            replay_terminal -->|Yes| replay_reload
            frame_swap --> broadcast_tick
        end

        tick_branch{"iFullTicks > 0?"}:::physics
        resends["ServerSessionRuntime::CompleteUpdate()<br/>(resends)"]:::network
        service_paused["ServerSessionRuntime::CompleteUpdate()<br/>(build navData + drain<br/>resync/new-subscription queues)"]:::network
        autosave["GameSaveLoad::TickAutosave()"]:::server

        pre_tick --> agent_drain --> save_load_replay --> wait_tick --> ts --> prepare_active --> physics_loop
        broadcast_tick --> tick_branch
        replay_reload --> tick_branch
        tick_branch -->|Yes| resends --> autosave
        tick_branch -->|"No (paused / zero-tick)"| service_paused --> autosave
    end

    display["ServerUpdateDisplayStats()"]:::server

    start --> msgs --> tick_frames --> display
    display -->|next iteration| start
```

## Frame Lifecycle

Server uses dual-buffered `pCurrent`/`pNext` on `CoordFrames`. Client uses the `snapshots[]` ring as its sole state buffer — the reconciler replays into the ring and the ring tail is read by render, active-set, camera, HUD, etc.

```mermaid
%%{init: {'theme': 'default'}}%%
flowchart LR
    classDef current fill:#d1fae5,stroke:#059669
    classDef next fill:#fef3c7,stroke:#d97706
    classDef render fill:#dbeafe,stroke:#3b82f6

    subgraph frame_struct ["Frame Structure"]
        frame["game::Frame"]
        fi["FrameInterpolate"]
        fpr["FramePostRender"]
        frame -->|owns| fi
        frame -->|owns| fpr
    end

    subgraph server_buffers ["CoordFrames (server)"]
        current["pCurrent"]:::current
        next_buf["pNext"]:::next
    end

    subgraph client_buffers ["CoordFrames (client)"]
        snap_stack["snapshots[] ring<br/>(sole state buffer)"]:::next
        tail["ring tail<br/>= read by render/camera/HUD"]:::current
    end

    subgraph render_buf ["Render Buffer (client)"]
        render_interp["mRenderInterpolates"]:::render
    end

    current -->|"AllocateAndCopy"| next_buf
    next_buf -->|"std::swap"| current
    snap_stack --> tail
    tail -->|"AllocateAndCopy + Update"| render_interp
```

## Collection Phase Participation

```mermaid
%%{init: {'theme': 'default'}}%%
graph TD
    classDef clientOnly fill:#dbeafe,stroke:#3b82f6
    classDef shared fill:#f3f4f6,stroke:#6b7280
    classDef game fill:#d1fae5,stroke:#059669
    classDef phase fill:#fef3c7,stroke:#d97706

    subgraph interpolate_phase ["Interpolate Phase"]
        direction LR
        i_engine["Explosions, Pushers"]:::shared
        i_client["AreaLights, Billboards,<br/>HexShields, PointLights,<br/>Puffs, Sounds, SmokeTrails,<br/>WindRadials, WindTrails"]:::clientOnly
        i_game["Players, Blasters,<br/>Missiles, Spaceships,<br/>Targets"]:::game
    end

    subgraph postrender_phase ["PostRender Phase"]
        direction LR
        pr_engine["Explosions, Pushers"]:::shared
        pr_client["AreaLights, Billboards,<br/>HexShields, PointLights,<br/>Puffs, Sounds, SmokeTrails,<br/>WindRadials, WindTrails"]:::clientOnly
        pr_game["Players, Blasters,<br/>Missiles, Spaceships,<br/>Targets"]:::game
    end

    fold_i["ForEachInterpolateUpdate"]:::phase
    fold_pr["ForEachPostRender phases"]:::phase

    fold_i --> interpolate_phase
    fold_pr --> postrender_phase
```

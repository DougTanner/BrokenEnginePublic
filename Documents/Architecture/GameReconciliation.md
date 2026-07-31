# Architecture: Game Reconciliation

> Maintained architecture reference for `Projects/BrokenEngineSandbox/Source/`; update when depicted reconciliation flow, client-loop integration, desync handling, or player-event parsing changes.

## Reconciliation State Machine

Each coord is reconciled independently through CRC fast-path, per-coord rollback, replay, catch-up, and snapshot ring buffer storage. The pipeline runs synchronously on the main thread from `ClientUpdate()` in a single pass (drops validated old frames, replays mismatches, and forward-sims empty-input ticks up to the post-advance `miTickCounter`). Per-coord work is parallelized via `common::gpMultithreading->Dispatch()`.

```mermaid
%%{init: {'theme': 'default'}}%%
flowchart TD
    classDef fastpath fill:#dcfce7,stroke:#16a34a
    classDef replay fill:#dbeafe,stroke:#3b82f6
    classDef error fill:#fee2e2,stroke:#ef4444
    classDef state fill:#fef3c7,stroke:#d97706

    START["ClientReconciler::Run()<br/>per CoordWork"] --> COORD["ReconcileCoord()<br/>per-coord orchestrator"]

    COORD --> CRC

    CRC{"CrcFastPathProcessCoord()<br/>validate snapshots against<br/>server CRCs"}:::fastpath
    CRC -->|"All matched"| FASTDONE["Advance confirmed state"]:::fastpath
    CRC -->|"Partial match"| PARTIAL["Advance confirmed by<br/>matched frames"]:::fastpath
    PARTIAL --> CRC
    CRC -->|"Gapped coord"| GAPSKIP["Skip, keep updates"]:::fastpath
    GAPSKIP --> CRC
    CRC -->|"Deferred coord"| DEFERRED["Skip, keep updates"]:::fastpath
    DEFERRED --> CRC
    CRC -->|"CRC mismatch"| MISMATCH["Force full reconcile"]:::error
    MISMATCH --> ROLLBACK
    CRC -->|"Stale missing snapshot"| STALENOSNAPSHOT["Force full reconcile"]:::error
    STALENOSNAPSHOT --> ROLLBACK
    CRC -->|"Deferred missing snapshot"| DEFNOSNAPSHOT["Skip, keep updates"]:::fastpath
    DEFNOSNAPSHOT --> CRC
    CRC -->|"Due pending full state"| ROLLBACK

    ROLLBACK["ReconcileRollbackCoord()<br/>restore confirmed frame<br/>into replay stack"]:::replay --> FINDRANGE

    FINDRANGE["ReconcileFindReplayRangeCoord()<br/>find uncapped consecutive<br/>server-frame endpoint"]:::state --> REACHABLE{"Due full-state tick<br/>reachable?"}

    REACHABLE -->|"Yes / none due"| REPLAY
    REACHABLE -->|"No: real update gap"| ADOPT["Adopt authoritative full state<br/>as logical ring base"]:::state

    REPLAY["ReconcileReplayCoord()<br/>replay consecutive range<br/>capped to target tick"]:::replay

    REPLAY --> CRCCHECK{"State CRC match?"}
    CRCCHECK -->|"Yes"| NEXTSRV{"More frames in<br/>replay range?"}
    CRCCHECK -->|"No"| DESYNC["Store desync info,<br/>return early"]:::error

    NEXTSRV -->|"Yes"| REPLAY
    NEXTSRV -->|"No"| CATCHUP
    ADOPT --> CATCHUP

    CATCHUP["ReconcileCatchUpCoord()<br/>simulate with empty inputs<br/>to target tick"]:::replay

    CATCHUP --> NEXTCOORD{"More coords?"}
    NEXTCOORD -->|"Yes"| COORD
    NEXTCOORD -->|"No"| CLIENTSTATE["ReconcileUpdateClientState()<br/>track client<br/>migration"]:::state

    CLIENTSTATE --> DONE["Writeback applied in-place<br/>on engine::CoordFrames"]
    FASTDONE --> NEXTCOORD
```

## Pending Full State Injection

Full states arrive during initial subscription and active-slot resync flows. A state ahead of the current target remains pending. Once due, it is injected at a reachable matching tick or adopted directly as the authoritative ring base when a gap prevents replay from reaching that tick. A state below the confirmed tick is stale and discarded:

```mermaid
%%{init: {'theme': 'default'}}%%
flowchart LR
    classDef injection fill:#dcfce7,stroke:#16a34a

    PFS["CoordFrames::pendingFullState"]

    PRE["ReconcileRollbackCoord()<br/>at confirmed frame"]:::injection
    MAIN["ReconcileReplayCoord()<br/>at matching replay tick"]:::injection
    GAP["ReconcileCoord()<br/>direct adoption past<br/>uncapped update gap"]:::injection
    STALE["Discard below-confirmed<br/>stale state"]

    PFS -->|"inject at confirmed"| PRE
    PFS -->|"inject at matching tick"| MAIN
    PFS -->|"becomes ring base in"| GAP
    PFS -->|"discard when stale"| STALE
```

## Extrapolation Mode

No longer a distinct mode. Forward simulation from the confirmed server state is performed inline by `ClientReconciler::Run()`'s catch-up pass (`ReconcileCatchUpCoord` simulates empty-input ticks up to the post-advance `miTickCounter`). The snapshot ring (`CoordFrames::snapshots[]`) is now the unified state buffer — there is no separate extrapolation stack or transition step.

## Main Loop Integration

Where reconciliation sits relative to render in the client main loop. There is no separate physics tick loop on the client — reconciliation's catch-up pass is the forward sim.

```mermaid
%%{init: {'theme': 'default'}}%%
flowchart TD
    classDef network fill:#dbeafe,stroke:#3b82f6
    classDef physics fill:#fef3c7,stroke:#d97706
    classDef render fill:#dcfce7,stroke:#16a34a
    classDef reconcile fill:#fce7f3,stroke:#db2777

    MSG["ProcessMessages()<br/>RawInput Update"] --> TICK_FRAMES

    subgraph TICK_FRAMES ["GameBase::ClientUpdate()"]
        POLL["ClientSession::Poll()<br/>network poll, ACK, flush"]:::reconcile

        STALL_CHECK{"IsStalled()?"}
        POLL --> STALL_CHECK
        STALL_CHECK -->|"Yes"| EARLY_RETURN["skip tick"]

        TICK["TickRealtime()<br/>compute iFullTicks"]:::physics
        PREPARE_ACTIVE["PrepareActiveSet()"]:::physics
        ADVANCE["Advance miTickCounter<br/>and mfCurrentTime"]:::physics
        RECONCILE_STEP["ClientSession::Reconcile()<br/>single pass: drops validated,<br/>replays mismatches,<br/>forward sims to target tick"]:::reconcile

        STALL_CHECK -->|"No"| TICK --> PREPARE_ACTIVE --> ADVANCE --> RECONCILE_STEP
    end

    subgraph RENDER_METHOD ["GameBase::Render()"]
        RENDER["Render interpolation +<br/>GPU rendering"]:::render
    end

    AUDIO["AudioManager::Update()"]

    TICK_FRAMES --> RENDER_METHOD --> AUDIO
    AUDIO --> MSG
```

## Desync Recovery and Optional Debug Frames

CRC mismatch always reports the differing CRCs. The manual `kbDesyncDebugFrames` switch is disabled by default; disabled builds immediately enter the normal recovery/disconnect policy without requesting or waiting for a full frame, while matching enabled client/server builds retain the per-field diagnostic comparison.

```mermaid
%%{init: {'theme': 'default'}}%%
sequenceDiagram
    participant Main as Main Thread
    participant Net as Client
    participant Server

    Main->>Main: ClientReconciler::Run() detects CRC mismatch
    Main->>Main: Deep-copy client Frame
    Main->>Net: SendDesyncReport()
    Net->>Server: Desync report

    alt kbDesyncDebugFrames enabled on client and server
        Main->>Net: SendDebugFrameRequest()
        Main->>Net: Set Client::mStateFlags kDesyncDebugMode
        Net->>Server: Debug frame request
        Note over Main: Polling and filling local slots from received state continue.<br/>ClientDesyncManager::IsStalled() gates physics,<br/>subscriptions, and reconciliation. Render and audio still run.
        alt Debug frame arrives before timeout
            Server->>Net: Debug frame response
            Main->>Main: ClientSessionRuntime::PollAndDrain() drains debug frame
            Main->>Main: LogDifferences(), then recover or disconnect
        else kDesyncDebugTimeout expires
            Main->>Main: Recover or disconnect without debug frame
        end
    else kbDesyncDebugFrames disabled
        Main->>Main: Run recovery/disconnect policy immediately
    end
```

## Player Event Parsing

Raw game packets are parsed into typed events. `kServerAssignPlayer` maps directly to `kAssigned`; `kServerPlayerState` packets carry a `PlayerStateWireType` that maps to the remaining event types.

```mermaid
%%{init: {'theme': 'default'}}%%
flowchart LR
    classDef wire fill:#fef3c7,stroke:#d97706
    classDef event fill:#dcfce7,stroke:#16a34a
    classDef func fill:#dbeafe,stroke:#3b82f6

    RAW["Raw Game Packets<br/>from Client"]
    PARSE["ParsePlayerEvents()"]:::func

    RAW --> PARSE

    subgraph wire_types ["PlayerStateWireType"]
        W0["kSpawned"]:::wire
        W1["kChangedFrame"]:::wire
        W2["kDied"]:::wire
    end

    subgraph event_types ["ReceivedPlayerEvent"]
        E0["kAssigned"]:::event
        E1["kSpawned"]:::event
        E2["kChangedFrame"]:::event
        E3["kDied"]:::event
    end

    PARSE -->|"kServerAssignPlayer"| E0
    PARSE -->|"kServerPlayerState"| wire_types
    W0 --> E1
    W1 --> E2
    W2 --> E3
```

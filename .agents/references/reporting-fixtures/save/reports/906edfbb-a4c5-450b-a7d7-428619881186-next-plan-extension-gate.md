Schema: be-agent-report/v1
Requested role: Opus/Terra adversarial extension-review gate for /next-plan Step 6
Actual executor: GPT-5 Codex
Fallback: none
Paired-review diversity: N/A
Worktree: <WORKTREE>
Session-start baseline: ca6f005addca80e8273cc7732e436fe351c1f71c
Plan/intent: Documents/Plans/Save/ServerSaveFailureReporting.md — gate replay failure-reporting sweep candidates H001 and H002 for fold versus surface

# Extension-review gate

## Result

Both candidates are **Fold**. Each is the same caller-observable failure-result propagation transformation as the refreshed plan, has an exact mechanical implementation with a closed call-site ripple, and changes no serialized bytes, deterministic state, wire data, guard/file affinity, or allocation-tracking boundary.

## H001 — Replay recording start continues after grid snapshot failure

**Decision: Fold**

### Same transformation

Confirmed. `GameSaveLoad::SyncReplayTick` calls `WriteGrid` at `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:304`, then unconditionally constructs every `mReplayWriters` entry at `:306-310` and emits `Recording started` at `:312`. Playback requires the snapshot: `SaveLoadReplay` calls `ReadGrid("F7.replay.grid", ...)` and returns on failure. After the target plan changes `WriteGrid` from `void` to the atomic-write `bool`, this start path would discard the newly available result and continue to publish success, exactly matching the plan's false-success transformation.

### Exact mechanical edit and ripple

Consume the new `WriteGrid` result at `GameSaveLoad.cpp:304`; on `false`, emit a replay-start write-failure error and return before the writer-construction loop and success log. On `true`, preserve the existing loop and `Recording started` log unchanged. No signature beyond the plan's already-declared `WriteGrid -> bool` change is needed, and no other caller or subsystem is affected. The existing `kSaveReplay` flag has already been cleared at `:301`, so returning follows the existing one-shot command consumption model and leaves recording inactive (`mReplayWriters` remains empty).

### Invariant exposure

- Determinism/CRC: none. The failure branch creates no writers and records no CRC stream; the success path is unchanged.
- `kiVersion` / `.pack` / manifest layout: none. No version, layout, or bytes written change.
- Replay/save format: none. It only refuses to begin a recording whose required existing grid component failed to commit.
- Client/server guard scope: none. `GameSaveLoad` remains under its existing `BT_SERVER` guards; no membership or affinity changes.
- Protocol/wire: none. This is the local debug replay toggle path and adds no message or acknowledgement.
- Main-loop allocation tracking: none. `SyncReplayTick` is already enclosed by `ScopedSuppressAllocationTracking` at `GameSaveLoad.cpp:292-294`; the branch adds only result consumption and a failure log inside that scope.

No undeclared invariant exposure or design choice remains. Fold into the target plan.

## H002 — Replay recording stop discards component write failures then logs stopped

**Decision: Fold**

### Same transformation

Confirmed. The stop path explicitly discards the manifest atomic-write result at `GameSaveLoad.cpp:322-343`, discards each replay-writer outcome because `DifferenceStreamWriter::Save` currently returns `void` at `:345-349`, discards `WriteVersionedFile`'s existing `bool` for metadata at `:359`, then unconditionally emits the sole normal completion log at `:361`. `DifferenceStreamWriter::Save` already computes all required sibling results (`bHeaderWritten`, `bFramesWritten`, `bChecksumsWritten`, and conditional `bFullFramesWritten`) and consolidates them into `failedFilename` in `Engine/Source/File/DifferenceStream.h:68-165`; it also removes that coordinate's sibling set on failure. The missing operation is solely exposing and aggregating those already-known outcomes before reporting replay persistence success.

### Exact mechanical edit and ripple

1. Change `DifferenceStreamWriter::Save` from `void` to `bool` and return `failedFilename.empty()` after its existing failure log/cleanup block. Do not alter write order, callbacks, cleanup, or serialized data.
2. In the replay-stop branch, initialize one aggregate from the manifest `WriteFileAtomically` result instead of `static_cast<void>`.
3. Execute every `rpWriter->Save` exactly as today and combine each returned value into the aggregate without short-circuiting later coordinate saves.
4. Execute `WriteVersionedFile` for `F7.replay.meta` exactly as today and combine its result into the aggregate.
5. Keep `mReplayWriters.clear()` and the recording-state transition unchanged; emit the existing `Recording stopped` success log only when the aggregate is true, and emit an error identifying replay component write failure when false.

Repository-wide search finds one `DifferenceStreamWriter` instantiation and one `Save` call, both in this replay path (`GameSaveLoad.h:50`, `GameSaveLoad.cpp:309,348`). The public template return-type ripple is therefore closed and mechanical. Global cleanup of manifest/meta/other coordinate files is not required to perform this plan's reporting transformation; the candidate neither weakens nor changes `DifferenceStreamWriter::Save`'s documented per-coordinate torn-set cleanup.

### Invariant exposure

- Determinism/CRC: none. All writers still save in the same loop with the same data, and checksum creation/cleanup is unchanged; only their computed success is returned.
- `kiVersion` / `.pack` / manifest layout: none. `kiReplayManifestVersion`, manifest fields/order, version headers, `.pack`, and `.manifest` asset formats are untouched.
- Replay/save format: none. Manifest, coordinate sibling sets, and metadata are written in the same order and with identical content. Return-value observation and conditional logging do not alter replay bytes.
- Client/server guard scope: none. `DifferenceStream.h` remains a shared template header, but repository search shows the writer instantiated only by server-guarded `GameSaveLoad`; no preprocessor guard or project membership changes.
- Protocol/wire: none. Replay stop remains a local debug-control operation and adds no packet or acknowledgement.
- Main-loop allocation tracking: none. The aggregation is allocation-free, and all existing path construction, writes, cleanup, and logs remain under `SyncReplayTick`'s `ScopedSuppressAllocationTracking` scope at `GameSaveLoad.cpp:292-294`.

No undeclared invariant exposure or unresolved design choice remains. Fold into the target plan.

## Residuals

None.

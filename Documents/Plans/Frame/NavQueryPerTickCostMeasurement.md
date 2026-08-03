<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-03T15:12:25.471Z","dependsOn":[]} -->
# Measure and Describe Per-Tick NavQuery Pathfinding Cost

## Context

The current `NavQuery` profile row reports a smoothed timer value, a rolling average, and a rolling maximum, but it
does not identify how many navigation queries produced a sample or how many of those queries entered A*.  The three
`NavQueryDirection` call sites in `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp`
(`:319`, `:375`, `:433`) are conditional on navigation mode and cadence.  `NavQueryDirection`
(`Engine/Source/Frame/NavQuery.cpp:616`) can return before pathfinding for a zero delta, empty navigation data,
start-inside escape, or direct line of sight; only its blocked-line branch enters `AStarPath` (`:697`), whose entry is
therefore a different population from total query calls.  A timer average or maximum cannot attribute that difference.

The server dispatches one `RunFrameTick` per active cell in `GameBase::BuildAndDispatchFrameTicks`
(`Engine/Source/GameBase.cpp:243-290`) and joins the dispatch before returning.  That join is the one safe boundary at
which a complete per-tick duration and both counts can be published without changing simulation order.  This Plan adds
profiling-only raw samples and runs one fixed server scenario to establish a like-for-like baseline, distributions, and
observed associations; it does not infer a cause, select an optimization direction, or implement an optimization.

## Design

### Raw instrumentation and latch ownership

Extend the existing `engine::ProfileManagerBase` CPU-timer state in
`Engine/Source/Profile/ProfileManagerBase.h/.cpp` as follows.  Every item in this subsection is under
`BT_SERVER` and `kbProfiling`; the existing client smoothing path is unchanged and has no raw-state reset owner.

1. Add generic raw CPU-timer slots keyed only by an `int64_t` timer index.  The game `ProfileManager` registers the
   `NavQuery` timer index in its server/profiling constructor, while `ProfileManagerBase` remains unaware of the game
   name.  On every successful `CpuStop`, the registered slot receives one invocation and its elapsed nanoseconds under
   the existing `mCpuTimerMutex`.  Keep this per-sample accumulator separate from the normal smoothing accumulator so
   `SmoothCpuTimers` and its overlapping rings cannot consume or reset a raw server-tick sample.
2. Expose neutral per-index APIs: `AddRawCpuTimerAuxiliaryCount(int64_t iTimer, int64_t iCount)` performs a relaxed atomic
   add to that slot's auxiliary count, `LatchRawCpuTimer(int64_t iTimer, bool bAccept)` snapshots and clears one slot,
   and `GetRawCpuTimer(int64_t iTimer)` reads its last accepted record.  The generic record uses
   `sampleSequence`, `sampleUs`, `invocationCount`, and `auxiliaryCount`; the latch advances the sequence and replaces
   the last record only when `bAccept` is true, while a discard clears pending values without advancing or replacing it.
   `LatchRawCpuTimers(bool bAccept)` is the neutral all-registered-slots wrapper used by `GameBase::ServerUpdate`, so
   no Engine file names `game::kCpuTimerPostRenderUpdateNavQuery`.
3. Do not instrument `AStarPath` directly.  Under `BT_SERVER && kbProfiling`, add an optional `bool* pOutEnteredAStar`
   to `NavQueryDirection` (and its declaration), initialize it to `false` at function entry, and set it to `true` only
   immediately before the blocked-line branch calls `AStarPath`.  In each of the three game-owned server call sites,
   close the existing `ScopedCpuProfile` scope immediately after `NavQueryDirection` returns, then call
   `AddRawCpuTimerAuxiliaryCount(game::kCpuTimerPostRenderUpdateNavQuery, bEnteredAStar ? 1 : 0)`.  The API performs the
   atomic add; this call is outside the timed scope and does not take `mCpuTimerMutex`, allocate, log, alter arguments,
   branches, floating-point operations, RNG draws, or output.  Client call sites pass no flag and retain their existing
   smoothing behavior.
4. `GameBase::ServerUpdate` computes an immutable acceptance gate immediately after `TickRealtime()` and before its
   pause handling or any burst cap: the original requested full-tick count is exactly `1`, `miTimeMultiply == 1`,
   `miTimeDivide == 1`, `kPaused` is clear, and `IsRecording()`, `IsReplaying()`, and `kSaveReplay` are all false.  This
   is also the no-catch-up condition; do not infer eligibility from the later mutated `iFullTicks`.  After every
   `BuildAndDispatchFrameTicks` returns (all workers joined), call `LatchRawCpuTimers(bAccept)` immediately before
   `FinalizeFrameTick()`.  A false gate discards pending values; a true gate publishes one record.  A zero-tick update
   has no dispatch window and therefore publishes nothing.  The latch is the sole owner of publication, uses the timer
   mutex for duration/count consistency, and exchanges auxiliary atomics at that same boundary.
5. The raw state, auxiliary accumulation, latch, and records stay outside `Frame`, CRC, RNG, navigation data,
   save/replay payloads, and version composition.  Existing client smoothing and all client behavior remain unchanged;
   no client reset or client `query_profile` fields are added.

### `query_profile` raw row

Extend `CommandQueryProfile` in `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServer.cpp` (`:350`) only for
the `NavQuery` timer row and only under `BT_SERVER && kbProfiling`.  Preserve the existing `currentUs`, `averageUs`, and
`maxUs` fields for compatibility, and map `invocationCount`/`auxiliaryCount` from the last accepted raw record to
`sampleSequence`, `sampleUs`, `queryCount`, and `aStarCount`.  The analyzer uses only the raw fields and treats a
repeated sequence as the same observed record; `averageUs` and `maxUs` are diagnostics, never independent cohorts.
The row and raw record are read under the same `mCpuTimerMutex` already used by `CommandQueryProfile`.

### Fixed measurement and descriptive analysis

Use `/agent-harness` against a normal server process in one process lifetime.  Do not pause, change timescale, record or
replay a save, or allow a burst update.  Drive the existing three-phase repro exactly:

1. `reset` the server, then inject exactly eight `SpawnPlayer` status changes at `coord:[0,0]`, each with
   `fleetWantedCoord:[0,-1]`.  After the eight spawns settle, call `query_players` for cell `[0,0]` and collect exactly
   eight player UUIDs.
2. After approximately 400 simulation ticks, send exactly one `inject_status_changes` batch containing exactly eight
   `UpdateFleet` objects.  Each object must have `coord:[0,0]`, one of the collected `playerUuid` values, and
   `fleetWantedCoord:[-1,0]`.  Assert the response has `injected == 8` and normal, nondeferred state.
3. After approximately 490 more ticks, repeat one `inject_status_changes` batch with exactly eight `UpdateFleet`
   objects, the same matching UUIDs and `coord:[0,0]`, targeting `fleetWantedCoord:[0,1]`; again assert
   `injected == 8` and normal, nondeferred state.  No single object updates all players.
4. After the final update settles, poll `query_profile` about every 25 ticks.  The server retains only one
   last-accepted, sequence-addressed raw record; the harness writes one entry to `Temp/navquery-attribution.json` only
   when a poll observes a sequence different from the previously observed sequence.  A repeated sequence writes no
   entry.  A sequence jump is an unobserved gap (not a rejected sample), so the file must record the observed sequence
   values and explicitly mark gaps as unobserved rather than claiming a complete stream or reconstructing rejected
   updates.  Each observed entry carries the raw fields and the scenario phase; update qualification is the server gate
   already recorded before dispatch, not a post-hoc rejection reason inferred from polling.

Discard the first 20 newly observed accepted records as warm-up.  Then collect two disjoint, equal cohorts of 20
  qualifying observed records each without restarting the process.  A cohort record qualifies only when
  `queryCount == 8` and `aStarCount == 8`; an observed record failing either test is retained as raw evidence and does
  not fill a cohort slot.  Do not add history or buffering to recover skipped sequences.  The report records each
  observed raw record and, for each cohort, arithmetic mean, median, maximum, count distribution,
  `sampleUs/queryCount`, and `sampleUs/aStarCount`.  The two cohorts are independent observed sequence ranges, not
  overlapping windows over the profile smoother.

Describe the observations without causal attribution: report raw query counts and A* incidence, aggregate and per-call
distributions, observed associations between count combinations and durations, variance, and limitations including
sparse observations and sequence gaps.  These observations do not identify frequency/trigger work, per-query/A* work,
or an actionable contributor; inconclusive evidence remains inconclusive.  The report outputs only the measured
like-for-like eight-unit baseline and its descriptive distributions/associations.  It must not invent or approve a
numeric performance budget, optimization direction, ratio, historical-build number, or unrecorded margin.  Plan 2
requires later evidence, an explicitly user-accepted numeric performance requirement, and a decision-complete direction;
this Plan does not draft or approve it.

## Critical files

- `Engine/Source/Profile/ProfileManagerBase.h` — `CpuTimer`, `ProfileManagerBase`, existing timer mutex, and the raw
  generic per-index raw state, registration, atomic auxiliary state, and latch API.
- `Engine/Source/Profile/ProfileManagerBase.cpp` — `CpuStop` accumulation and the latch/discard implementation.
- `Engine/Source/Frame/NavQuery.h` — profiling-only optional `NavQueryDirection` A* entry output.
- `Engine/Source/Frame/NavQuery.cpp` — `NavQueryDirection` flag initialization and the flag set immediately before
  `AStarPath` at `:697`; no query behavior changes and no direct profile-manager dependency.
- `Engine/Source/GameBase.cpp` — `ServerUpdate` and `BuildAndDispatchFrameTicks` join boundary (`:243-290`) where the
  pre-mutation one-tick acceptance gate and neutral raw latch are placed immediately before `FinalizeFrameTick`.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServer.cpp` — `CommandQueryProfile` (`:350`) raw fields on
  the `NavQuery` row, explicitly server/profiling-only.
- `Projects/BrokenEngineSandbox/Documents/AgentHarness.md` — documentation of the server `NavQuery` raw row fields
  (`sampleSequence`, `sampleUs`, `queryCount`, and `aStarCount`), accepted tick sequencing, repeated reads, rejected
  gates that do not advance, unobserved sequence gaps, and the unchanged client `query_profile` schema; scope remains
  the four debug-only raw fields.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp` — the three existing
  `ScopedCpuProfile`/`NavQueryDirection` call sites (`:319`, `:375`, `:433`) and their server/profiling-only auxiliary
  count calls after the timed scopes.
- `Projects/BrokenEngineSandbox/Source/Profile/ProfileManager.cpp` — server/profiling registration of the game NavQuery
  timer index with the neutral base manager.
- `Projects/BrokenEngineSandbox/Source/Profile/ProfileManager.h` — read-only enum/name contract for
  `kCpuTimerPostRenderUpdateNavQuery` (`:20-43`).

## In scope

- The `BT_SERVER && kbProfiling` generic per-index raw timer invocation/duration accumulator, auxiliary A* entry count,
  monotonic last-accepted record, timer registration, and post-dispatch latch/discard API described above.
- The profiling-only `NavQueryDirection` entry flag, the three game-owned server call-site increments, and the server
  one-tick qualification gate; no navigation branch or call-site condition is edited.
- The four raw `query_profile` fields on the existing `NavQuery` row and their mutex-protected read path, all explicitly
  server/profiling-only.
- The fixed eight-player three-phase harness run, warm-up discard, two disjoint n=20 cohort collection, qualification
  rule (`queryCount == 8 && aStarCount == 8`), sparse observed-record/gap evidence capture, and descriptive reporting of
  raw rows, A* incidence, aggregate/per-call distributions, observed associations, variance, and limitations.  The
  output is the measured baseline and descriptive analysis only; no causal or actionable attribution, optimization
  direction, or numeric budget is selected here.  Inconclusive evidence remains inconclusive.
- The corresponding `AgentHarness.md` documentation for the four debug-only server raw fields, accepted tick sequence,
  repeated reads, rejected gates that do not advance, unobserved gaps, and unchanged client `query_profile` schema.
- Static/compile proof that profiling fields, flags, latch state, and raw query schema stay outside deterministic state,
  independent replay record/playback checksum validation, live client/server no-confirmed-desync checks, and clean
  client and server builds for the instrumentation.

## Out of scope

- Any navigation optimization or implementation Plan 2: changing trigger containment, recompute cadence, A* visibility,
  broad-phase dimensions, obstacle inflation, path caching, asynchronous execution, or moving pathfinding off the
  simulation thread. The measurement result chooses none of these.
- Historical or pre-change-build baselines, including the earlier 17.05-us value: early-return and livelocked units did
  not perform the same A* work, so that population is not a valid cohort.
- A paired old/new build or live path, shadow execution, or dual-process comparison: it doubles pathfinding and changes
  dispatch timing and therefore cannot provide an independent baseline.
- Existing `averageUs`/`maxUs` rolling values as cohorts: their windows overlap and expose no raw sequence or count
  qualification.
- Client raw accumulation, A* output flags, latching, records, resets, and `query_profile` fields; client behavior and
  existing smoothing remain unchanged.
- Changes to `Frame`, shared CRC, RNG draw order, navigation output, `NavData` content/format, `.pack` data, wire
  protocol, save/replay payloads, `game::Frame::kiVersion`, or `engine::kiNavDataVersion` (`Engine/Source/Frame/NavBuild.h:15`).
- New unit tests, broad profiler refactors, unrelated profile rows, or changes to the AgentHarness protocol beyond the
  four debug-only raw fields on the existing row.

## Risk tier and invariants

**Tier 3.** The measurement code crosses the Engine profile manager, server frame-dispatch join, Engine navigation
query, and Agent query surfaces, and aggregates data from parallel cell workers. The trigger is threading/aggregation
coordination, even though the output is profiling-only and outside deterministic state.

- Every `NavQueryDirection` invocation at the three existing call sites contributes exactly one invocation count when its
  existing timer scope stops; a call that reaches the blocked-line branch sets the profiling-only flag immediately
  before `AStarPath`, and its game caller contributes exactly one atomic auxiliary count, including an A* miss. Early
  `NavQueryDirection` returns contribute no auxiliary count.
- A raw sample is published only after all active-cell dispatch workers have joined. Duration and invocation count are
  one timer window; auxiliary atomics are exchanged at the same latch. A rejected pause/burst/save/replay update clears
  pending values and cannot contaminate a later accepted sample; skipped sequence numbers are unobserved, not rejected
  samples.
- Every raw accumulator, flag, auxiliary call, latch, record, and raw `query_profile` field is server/profiling-only
  (`BT_SERVER` plus `kbProfiling`). Instrumentation does not add or reorder RNG draws, read wall-clock state in
  simulation, write Frame/CRC members, alter navigation output, change serialization or wire bytes, or bump any version.
  Static/compile proof keeps the profiling fields, flags, latch state, and raw query schema outside `Frame`, CRC, RNG,
  wire, save, and replay state; independent replay record/playback checksum validation and live client/server
  no-confirmed-desync checks provide the runtime evidence, while client smoothing remains exactly as before.
- `sampleSequence` is monotonic and immutable for a published record; `query_profile` reads a coherent record under the
  existing profile mutex. No overlapping smoother statistic is treated as a cohort.

## Acceptance criteria

1. Client and server builds compile cleanly with the instrumentation, and the changed source exposes no new frame,
   CRC, RNG, save/replay, NavData, wire, `.pack`, or version bytes.
2. `query_profile` returns `sampleSequence`, `sampleUs`, `queryCount`, and `aStarCount` on the `NavQuery` row; polling
   without a newly observed accepted server tick repeats the same sequence and values, while each accepted normal
   one-tick update advances the sequence exactly once.  Polling may skip sequence numbers; those gaps are reported as
   unobserved rather than classified as rejected updates.
3. In the fixed eight-player repro, at least 40 qualifying samples are collected after the 20-sample warm-up in one
   process, split into two disjoint n=20 cohorts. Every cohort member has `queryCount == 8` and `aStarCount == 8`; all
   observed records failing qualification remain in the raw evidence file, and every sequence gap is marked unobserved.
4. The report presents raw sample rows, both cohort statistics, A* incidence, aggregate and per-call distributions,
   observed associations, variance, and limitations, including the eight-unit like-for-like baseline.  It explicitly
   makes no causal or actionable attribution, optimization-direction, or numeric-budget decision and does not use
   `averageUs`/`maxUs`, a historical build, a paired build/path, or an unstated percentage margin.
5. The analysis states what the observed records do and do not show: raw counts, A* incidence, distributions, observed
   associations, variance, and limitations.  It does not claim that any pattern identifies frequency/trigger work,
   per-query/A* work, or an actionable contributor; inconclusive evidence remains inconclusive.  Failure to qualify 40
   samples is a measurement failure, not permission to infer a cause or budget.  No optimization direction or numeric
   requirement is selected here.
6. At least 2000 normal client/server ticks run with zero `CONFIRMED DESYNC after full rollback/replay`; static/compile
   proof verifies profiling fields, flags, latch state, and raw query schema stay outside `Frame`, CRC, RNG, wire,
   save, and replay state.  Independent replay record/playback checksum validation and live client/server
   no-confirmed-desync checks show no instrumentation-induced divergence.  No paired build/path or new protocol is used.
   The scenario records zero unexpected `kNavData` A*-miss warnings during the measured window.
7. Stop after presenting the measured baseline and descriptive analysis; this Plan never infers an actionable cause,
   selects an optimization direction, or approves a numeric requirement.  A separate implementation Plan 2 may be
   created only after later evidence, explicit user acceptance of a numeric performance requirement, and a
   decision-complete direction.  That Plan must name the exact direction and symbols, the user-accepted numeric
   requirement, and compatibility evidence. A behavior-only change that alters CRC'd output owns the `Frame.cpp` base
   of `game::Frame::kiVersion`; a `NavData` content/format change owns only `engine::kiNavDataVersion`; a proven
   bit-identical work removal bumps neither and must prove bit identity.  If those conditions are not all met, close
   this Plan with no implementation Plan.
8. No unit tests are added; the targeted checks are the builds, raw profile schema/sequence checks, fixed harness
   measurement, and determinism/no-desync evidence above.

## Verification

Runtime verification uses `/agent-harness` on a normal server one-tick update stream and the fixed `reset`/eight-player/
three-phase `UpdateFleet` sequence in Design. Poll `query_profile` for newly observed `sampleSequence` values and save
only those raw rows, qualification decisions, and explicitly unobserved sequence gaps to
`Temp/navquery-attribution.json`; do not claim a complete stream or rejected-update records.  Inspect
`get_logs {"category":"NavData"}` for the A*-miss check and the client/server logs for desync and replay outcomes. Run
the client and server builds through `/compile`.  Record static/compile proof that the profiling fields, flags, latch
state, and raw query schema stay outside deterministic state, then independently validate replay record/playback
checksums and live client/server no-confirmed-desync; do not use a paired build/path or add a protocol.

## Coordination

- `Documents/Plans/Frame/NavInflateMarginUvRelative.md` changes obstacle geometry and therefore both vertex counts and
  containment-trigger frequency. If that Plan lands before this measurement, rerun the complete baseline and cohorts on
  the landed tree; do not carry raw values across the geometry change. This measurement does not depend on or co-land
  with that Plan.
- Plan 2, if created after this result, is a separate decision-complete Plan and must not be drafted or claimed as part
  of this measurement. Its own risk tier follows the chosen change: Tier 3 for CRC/version/NavData/serialization or
  threading effects, and only a proven bit-identical, behavior-preserving optimization may be Tier 2.

## Notes

- Line references are from the current tree on 2026-08-03 and must be refreshed if symbols move before implementation.
- The current source's `CommandQueryProfile` already locks `mCpuTimerMutex` while reading timer rows; the raw row uses
  that same lock rather than creating a second synchronization domain.
- This Plan is deliberately descriptive measurement only.  Its measured baseline, raw counts, A* incidence,
  distributions, observed associations, variance, and limitations are outputs; it does not infer a cause, identify an
  actionable contributor, select an optimization direction, or approve a numeric budget.  Inconclusive evidence remains
  inconclusive.  A later Plan requires later evidence, explicit user acceptance of the numeric requirement, and a
  decision-complete direction.  The rejected malformed Plan is not a dependency or source of baseline evidence.

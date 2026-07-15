Schema: be-agent-report/v1
Requested role: Fable
Actual executor: Sol
Fallback: none
Paired-review diversity: N/A
Worktree: <WORKTREE>
Session-start baseline: ca6f005addca80e8273cc7732e436fe351c1f71c
Plan/intent: Documents/Plans/Save/ServerSaveFailureReporting.md; current not-approved canonical synthesis for /next-plan; smallest complete correctness fix

## Findings

F001

> `Documents/Plans/Save/ServerSaveFailureReporting.md:46` — **scope contradiction (HIGH)** — reserved-device rejection is a mandatory execution step and part of the selected Order deliverable, but the acceptance criterion still makes it conditional with “(If rider taken)”; an implementation can therefore satisfy the written acceptance criteria while omitting selected scope — evidence: `Documents/Plans/Order.md:21` — proposed improvement: resolve the rider as taken in the canonical plan, remove the conditional wording everywhere, and make rejection of the complete listed reserved-name set mandatory.

F002

> `Documents/Plans/Save/ServerSaveFailureReporting.md:42` — **acceptance/invariant coverage (HIGH)** — the acceptance criteria cover only agent save propagation and the conditional filename rider; they omit the client-requested warning and both auto-folded replay behaviors, even though replay start must not create writers after a failed grid write and replay stop now changes the public `DifferenceStreamWriter::Save` contract plus manifest/writer/metadata lifecycle semantics — evidence: `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:299` — proposed improvement: add observable criteria for `kClientSaveRequest` failure logging, failed replay start (flag consumed, no writers, no success log), failed replay stop (manifest, every writer, and metadata all attempted; writers cleared; no success log), `DifferenceStreamWriter::Save == false` after its existing sibling cleanup, and successful replay load after an ordinary start/stop; explicitly state replay/allocation-tracked-path exposure and that file bytes, `Frame::kiVersion`, manifest version, CRC, and wire contracts remain unchanged.

F003

> `Documents/Plans/Save/ServerSaveFailureReporting.md:32` — **error-log semantics (MEDIUM)** — both folded replay steps say only “log” failure, leaving category/level unspecified; choosing the surrounding `kDebug` level would hide the only replay persistence failure signal at the default `kInfo` threshold and undermine the false-success-reporting goal — evidence: `AGENTS.md:122` — proposed improvement: require `kDefault`/`kError` for replay-grid start failure and aggregate replay-stop persistence failure, keep `Recording started`/`Recording stopped` at their existing `kDebug` level only on success, and use allocation-free path formatting in the new Engine/Game log calls.

F004

> `Documents/Plans/Save/ServerSaveFailureReporting.md:25` — **verification omission (HIGH)** — the plan has no verification section and no deterministic way to exercise a real `WriteFileAtomically == false`, the dispatcher envelope, replay non-short-circuiting, or the unchanged replay format; compilation alone cannot prove the user-visible contract — evidence: `Engine/Source/File/FileManager.h:219` — proposed improvement: add server and client compile checks; an agent-harness valid-save success case; a deterministic appdata obstruction (for example, pre-create the selected target’s `.tmp` path as a directory) proving `save` returns `ok:false` with no `result.file`; reserved-name cases including mixed case and extensions; a client quicksave obstruction proving the warning; replay-grid/manifest/meta obstruction cases proving flags/writers/logs and that later writes are still attempted; and an unobstructed record-stop-playback smoke proving the emitted bytes still load without a version change. Preserve/restore any pre-existing appdata files used by these tests.

F005

> `Documents/Plans/Save/ServerSaveFailureReporting.md:5` — **queue metadata drift (MEDIUM)** — the canonical synthesis auto-folded replay start/stop and the shared Engine `DifferenceStream.h` API, but the live Order row still describes only server-save propagation and the filename rider, so it is no longer the required one-line summary of what lands — evidence: `Documents/Plans/AGENTS.md:36` — proposed improvement: refresh the Order row notes before approval to include replay lifecycle result propagation and the `DifferenceStreamWriter::Save` return change; retain the existing `GameSaveLoad` overlap warning with `Save/DirectLoadTickClockRetention.md`, whose touched `ReadGrid`/`ServerLoad` regions remain disjoint.

F006

> `Documents/Plans/Save/ServerSaveFailureReporting.md:31` — **validation algorithm ambiguity (MEDIUM)** — “matching the stem before an extension” does not define whether the comparison stops at the first dot; a direct use of `std::filesystem::path::stem()` strips only the final extension and can miss a reserved base in a multi-extension input such as `NUL.replay.save`, contrary to the source plan’s “before any extension” intent — evidence: `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServer.cpp:25` — proposed improvement: specify ASCII case-folding of the substring before the first `.` and compare that base against `CON`, `NUL`, `PRN`, `AUX`, `COM1`-`COM9`, and `LPT1`-`LPT9`; add bare, mixed-case, single-extension, and multi-extension acceptance cases.

## Verified assumptions

- Repository-wide call-site search found only the declared `GameSaveLoad::ServerSave` overloads and the agent/server-session callers, plus only one `DifferenceStreamWriter::Save` caller at `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:348`; the planned signature ripple is complete.
- Clearing `kSaveReplay` before the start/stop work already gives one-shot flag consumption. Returning before writer creation on failed start leaves `IsRecording() == false`; clearing writers after all stop writes preserves the existing stop lifecycle.
- Non-short-circuit aggregation is implementable with separate per-write results or `bCurrentWrite && bAllWritten` (write call on the left); the plan should reject `bAllWritten && write()`.
- `WriteVersionedFile` already returns the metadata atomic-write result (`Engine/Source/File/FileManager.h:255`), and `DifferenceStreamWriter::Save` already computes every sibling result and failure cleanup (`Engine/Source/File/DifferenceStream.h:73`). Returning those booleans changes no serialized field or byte ordering and requires no version bump.
- Runtime allocations remain covered by the existing `ScopedSuppressAllocationTracking` in `GameSaveLoad::SyncReplayTick` (`Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:294`) and `AgentCommandServer::Drain` (`Engine/Source/Agent/AgentCommandServer.cpp:194`); new replay logging must retain the repository’s allocation-free formatter discipline.
- The only live shared-file queue overlap is `Save/DirectLoadTickClockRetention.md` in `GameSaveLoad.{h,cpp}`; its direct-load regions are disjoint from this plan’s save/replay regions, so the existing co-schedule/refresh warning is sufficient.

Files changed: none
Functions/regions touched: none
Residuals: none

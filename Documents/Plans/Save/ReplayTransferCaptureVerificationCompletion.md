<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-29T02:59:46.448Z","dependsOn":[]} -->
# Complete Replay Transfer Capture Verification

## Context

`ReplayTransferCaptureCompleteness` implemented transfer-aware replay recording, delayed destination activation, exact terminal data, manifest-v3 SHA-256 inventory binding, and agent fixtures. The landing session was explicitly directed to defer its remaining validation work.

The last runtime attempts proved that ordinary agent commands are drained only on advancing simulation ticks: an external pause sent after observing writer tick `E + 1` arrives after writer tick `E + 2`. Evidence is retained under `Temp/ReplayTransferCaptureCompleteness-20260729T0254-correct-order-final/`. The current tree therefore adds optional debug-only `pauseAfterWriterInput` handling to `replay_transfer_fixture`, but that late change has not been propagated, styled, rebuilt, reviewed, or exercised. The complete A-H replay acceptance matrix also remains unfinished.

This is Change Workflow Tier 3 because replay input, deterministic frame CRC validation, persisted replay identity, and cross-frame activation/terminal ordering are exposed.

## Design

1. Audit the existing `pauseAfterWriterInput` implementation. Preserve absent/false behavior. When true, it must arm during active recording or a paused pending start, survive only a successful start transition, pause exactly after the first normal writer input, consume once, and clear through every reset/cancel/failure/stop lifecycle.
2. Run `/update-affected-code`, `/code-style-review`, and `/update-claude-docs` for the late C++ change. Keep the fixture state debug-only and avoid changing replay file formats or production simulation behavior.
3. Run fresh focused C++ correctness and Tier-3 adversarial reviews for the late fixture state, command trust boundary, and pause ordering. Re-run documentation coherence for the fixture schema/A-B recipe and manifest-version rejection wording.
4. Build Debug x64 client and server with the canonical Shared data oracle and `RunDataPacker=false`. Retain the already-proven DataPacker result unless a reached dependency changed.
5. Run the documented replay acceptance matrix A-G using `pauseAfterWriterInput:true`, then the stopped-server manifest-v3 integrity matrix H1-H5 and record the H6 limitation. Require a completed playback loop, exact transfer counts/activation/retirement, no new CRC/checksum/desync/read errors, rejection before live-state adoption, and manifest-last failure behavior.
6. Resolve any concrete build, review, or runtime failure in scope and rerun only invalidated checks before final acceptance.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.h`
- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp`
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServer.cpp`
- `Projects/BrokenEngineSandbox/Documents/AgentHarness.md`
- `Engine/Source/File/DifferenceStream.h`
- `Engine/Source/File/FileManager.h`
- `Engine/Source/File/FileManager.cpp`

## Out of scope

- Authentication against an actor that can rewrite the manifest and every replay artifact.
- Protection against concurrent file mutation after replay preflight.
- New replay compatibility paths; manifest v2 remains rejected.
- Product-facing pause or recording controls.
- Refactoring large replay functions or unrelated cleanup.

## Acceptance criteria

- The late `pauseAfterWriterInput` state and command path pass propagation, style, documentation, focused correctness, and adversarial review with no unresolved finding.
- Debug x64 client and server builds pass against an unchanged canonical Shared data oracle.
- Replay fixture A-G and manifest integrity H1-H5 pass with retained command, log, state, mutation, cleanup, and oracle evidence; H6 is reported as the explicit security boundary.
- A proves event harvest at `E`, first writer input and automatic pause at `E + 1`, `writerInputCount == 1`, and exact transfer counts. B proves terminal empty input at `E + 2` without incrementing that count.
- Playback proves destination absence through `E`, activation and exact transfer at `E + 1`, recorded terminal retirement, full reader consumption, checksum validation, and an `End replay <tick>, looping` marker.
- Every manifest mutation rejects without adopting grid, fleet, reader, coordinate, or next-global-ID state. Version 2 may use its explicit version-rejection log; corrupt v3 data uses the corrupt-replay abort path.

## Notes

- Prior immutable C++ review and Tier-3 adversarial review passed before the late automatic-pause hook. The only accepted C++ finding, parallel reader booleans, was replaced with typed `common::Flags` and passed focused re-review.
- Prior build logs and runtime artifacts are under `Temp/AgentBuildLogs/` and `Temp/ReplayTransferCaptureCompleteness-*`; they are diagnostic context, not substitutes for the acceptance run against the final bytes.

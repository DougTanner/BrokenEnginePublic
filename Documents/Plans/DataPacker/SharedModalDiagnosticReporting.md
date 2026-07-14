# Shared DataPacker modal diagnostic reporting

## Context

DataPacker has several user-blocking diagnostics whose complete text exists only in a Windows `MessageBox`. During automated verification, `Unexpected output reparse point` was visible only in the modal while captured stdout contained `Data Packer` and the process exited 1, forcing manual transcription before the failure could be diagnosed.

The root cause is split ownership. `Main.cpp`'s `Quit` (`DataPacker/Source/Main.cpp:376-380`) flushes stdout and calls `MessageBox`, and it serves export-failure aggregation plus the top-level `std::exception` and unknown-exception catches. `FileManager::MaterializeOutput` independently calls `MessageBoxW` for insufficient disk space and low-disk OK/Cancel (`DataPacker/Source/FileManager.cpp:528-549`). The export future catch already logs each exception before constructing a second aggregate modal (`Main.cpp:215-247`), while the insufficient-disk path shows its own modal and then throws into the top-level modal catch. These paths can drift, duplicate diagnostics, and leave an agent-readable stream without the actionable error.

DataPacker already validates linked-worktree identity in `FileManager::InitializeWorktreeOutputs`: Git's worktree/common-directory agreement and the expected output path are proven before output-link validation continues (`FileManager.cpp:367-408`). Agent workflows own errors in that validated linked-worktree mode and must not block on popups. An ordinary primary checkout remains interactive. If identity is unavailable or ambiguous, including an exception before the proof point or before `FileManager` finishes construction, behavior must fail closed by retaining the modal.

Originating acceptance gap: the active DataPacker legacy-cache cleanup runtime verification could not retrieve a modal-only fatal error without user relay. User design authority requires one shared helper function or class, a single structured record/message construction path feeding agent-readable logging and the modal sink, and popup suppression in validated linked worktrees.

## Design

1. Add one DataPacker-local diagnostic reporter (for example `DiagnosticReporter.{h,cpp}`) shared by `Main.cpp` and `FileManager.cpp`. Its input is one structured diagnostic record, not parallel preformatted strings. The record carries severity/log category, title, nonempty message, operation/call-site identifier, relevant path fields, and an optional Win32 error supplied by the caller. It also carries the existing button contract (`OK` or `OK/Cancel`) and icon/severity. The reporter constructs the human-readable modal text and one stable agent-readable diagnostic line from that record so both sinks contain the same facts.
2. Make the reporter own emission and outcome. It emits the complete initial diagnostic record exactly once through the existing `LOG` backend at the corresponding level (`kError` for fatal/error diagnostics, `kWarning` for low-disk confirmation) and flushes the console stream before any modal blocks. Interactive records use `outcome=pending` because the selection is not yet known; after `MessageBox` returns, emit a separate concise result event with the operation identifier and selected outcome, without duplicating the diagnostic payload. Noninteractive records include their forced outcome in the initial line and need no completion event. The reporter returns a typed button-equivalent result. Do not add a logging backend, stack capture, or generic exception framework.
3. Give the reporter a process-lifetime execution context independent of `gpFileManager`, defaulting to interactive/identity-unknown. As soon as `InitializeWorktreeOutputs` has proven that the repository is a linked worktree with consistent common-directory metadata and the expected output destination, set the reporter context to validated-linked-worktree before later output/link checks can throw. The state must remain available after `FileManager` destruction so top-level catches can use it. Do not rerun Git discovery in the reporter. Ordinary-primary and every unavailable, malformed, inconsistent, early-failure, or otherwise ambiguous identity retain interactive modal behavior.
4. In validated linked worktrees, emit the same diagnostic record but skip `MessageBox`. Fatal `OK` reports return the existing acknowledged outcome and continue into the existing failure return/throw flow. `OK/Cancel` warnings conservatively return Cancel, record that deterministic noninteractive outcome in the agent-readable line, and preserve the current cancellation semantics (no materialization or output mutation after cancellation). In interactive mode, preserve the current titles, icons, button sets, system-modal behavior, and actual user-selected result.
5. Route all current modal paths through the reporter: export-failure aggregate, top-level `std::exception`, top-level unknown exception, insufficient-disk error, and low-disk warning/cancel. Unknown exceptions use a stable nonempty message such as `Unknown non-standard exception escaped DataPacker` and an explicit top-level operation identifier.
6. Establish exactly-once ownership. The export aggregate reporter owns the complete export diagnostic; remove the per-future duplicate error LOG and redundant failure banner while retaining every asset/message in the aggregate record. A directly reported diagnostic that must still unwind (such as insufficient disk) must carry an explicit already-reported marker/result through the existing failure flow so the top-level catch does not emit it again. Unreported exceptions are owned once by the top-level reporter. Do not infer prior reporting by comparing strings.
7. Populate structured context at the source when available. Disk-space diagnostics name the materialization operation and destination/source paths plus required, available, and projected byte values in the message/record. Win32 API failures capture `GetLastError()` immediately and supply it with the relevant root path rather than losing it before the top-level report. Other exceptions retain their exact `what()` text and receive a stable top-level operation identifier; callers are not required to invent unavailable paths or error codes.
8. If the reporter uses new source/header files, add both to `DataPacker.vcxproj` and `DataPacker.vcxproj.filters` in the matching Source filter. Keep the interface DataPacker-local; no Common or engine API is warranted.

## Critical files

- `DataPacker/Source/Main.cpp` — `Quit`, `RunExportJobs<T>`, and `main`'s top-level exception handlers; replace modal-only and duplicate export reporting with the shared reporter.
- `DataPacker/Source/FileManager.cpp` — `FileManager::InitializeWorktreeOutputs` validated-linked-worktree proof point and `FileManager::MaterializeOutput` disk-space error/warning paths.
- `DataPacker/Source/DiagnosticReporter.h` / `DiagnosticReporter.cpp` (proposed) — structured record, durable execution context, sink selection, exactly-once marker, and typed button result.
- `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj` / `DataPacker.vcxproj.filters` — add new reporter files if created.
- `DataPacker/Source/AGENTS.md` — document linked-worktree noninteractive diagnostic behavior and shared reporter ownership.

## Out of scope

- New logging backends, telemetry, stack traces, minidumps, or general-purpose Common diagnostics.
- Reworking non-modal routine logs or changing export/job error recovery beyond eliminating duplicate modal-record emission.
- Adding command-line switches to force interactive/noninteractive behavior; mode comes from the existing validated worktree identity.
- Changing disk-space thresholds, materialization copy/link semantics, or output validation policy.
- Unit tests.

## Acceptance criteria

- Every current DataPacker modal diagnostic emits one complete, stable agent-readable initial record exactly once before a modal can block, including severity/category, title, nonempty message, operation, relevant paths/byte context, optional caller-supplied Win32 error, and `outcome=pending` for interactive prompts or the forced outcome for noninteractive handling. After an interactive modal returns, one separate concise result event records the operation and selected outcome without repeating the diagnostic payload.
- A validated linked-worktree run never opens a diagnostic `MessageBox`. Fatal paths retain exit code 1. Low-disk OK/Cancel deterministically chooses Cancel, logs that forced outcome, and leaves the recognized output link/materialization source unchanged.
- A primary ordinary checkout and all unknown/ambiguous/early identity paths preserve the existing interactive titles, icons, system modality, buttons, and button-result behavior.
- The export aggregate and insufficient-disk paths emit exactly one complete record each; top-level exception handling does not re-report an already-reported failure. Unknown exceptions emit a stable nonempty message.
- Capture stdout/stderr for representative top-level exception, multi-export aggregate, insufficient-disk failure, and low-disk warning/cancel paths. Verify the exact actionable record is present without manual relay. Exercise linked-worktree paths without UI; exercise interactive behavior by programmatically dismissing the modal or through a narrow reporter-level executable path. Do not add unit tests.
- Compile DataPacker Release through `/compile`; verify any new `.cpp`/`.h` membership and filters through `/update-vcxproj`.

## Notes

- Offline DataPacker-only control flow. No simulation determinism/CRC, replay, wire protocol, client/server guard, shader, allocation-tracked main-loop, `.pack` layout, or `kiVersion` exposure.
- The reporter's process-lifetime context must be monotonic: unknown/interactive can become validated-linked-worktree only at the existing proof point and must not fall back when `FileManager` is destroyed.
- Live verification exposure is deliberate: modal suppression is specifically for agent-owned validated worktrees, while ordinary developer runs remain interactive.

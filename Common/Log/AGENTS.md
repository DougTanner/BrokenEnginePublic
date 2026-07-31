# Common Log

Shared logging, formatting, diagnostic-file, and difference-reporting implementation. Call-site level meanings and allocation rules remain at the Common hub.

## Contracts

- `LOG` first applies each project's minimum log level per category, fixed at compile time, then the atomic runtime threshold. Runtime levels default to `kInfo` (`kVerbose` in DataPacker); `kTemp` is the one exception and defaults to `kVerbose` in every build, so its messages always emit. The agent command may raise or lower them only within the compiled range.
- Threads format into their `ThreadLocal` buffer or a thread-local fallback. Emission is serialized; the crash snapshot ring does not wrap, while the ring that agent queries read — one shared ring buffer spanning all categories — does. Keep formatting allocation-free before entering either ring.
- Tick scopes, and scopes that set their indent level directly rather than by nesting, propagate simulation context across worker dispatch. `ScopedLogIndent` is the separate delta-based scope for ordinary nesting.
- `LogFormatters.h` owns formatters for Common-visible types. Paths, wide strings, vectors, and precision-sensitive floats use workbuffer-backed wrappers; higher-layer types stay with their owning aggregation hub.
- `DiagnosticLog` writes explicitly initialized per-file diagnostics with allocation tracking suppressed. `LogDifference` compares deterministic state under a scoped section label; add structured comparisons there instead of building ad hoc desync strings. It labels its first operand Client and its second Server positionally, not by which process produced either side, so a replay comparison of two server-side snapshots still carries those labels and any summary line emitted beside a comparison must order its operands the same way.
- `EnableLogFile` mirrors emitted lines to a flushed file sink. Treat sink setup as startup/diagnostic work, not a tracked-loop operation.

## Failure and Concurrency Rules

Logging may run during exception handling. Do not introduce recursive logging, heap-dependent crash formatting, or unsynchronized DbgHelp use. The engine's debug-string emission is marked so the vectored exception handler does not re-log it.

When adding a category, update the enum, names, per-project compile floors, runtime-level storage, and any agent-facing parsing together.

## See Also

- `../Threading/AGENTS.md` - thread-local buffers and context lifetime

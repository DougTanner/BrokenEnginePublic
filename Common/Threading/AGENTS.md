# Common Threading

Worker-pool, persistent-worker, and per-thread runtime state shared by engine and tooling code.

## Worker Pool

The primary `Multithreading` instance owns `gpMultithreading`. The named-pool constructor creates an independent pool without replacing that singleton. Each worker owns a `ThreadLocal` and its workbuffer.

`Dispatch` is non-reentrant per pool. It waits for every assigned worker before returning or rethrowing the first captured exception, so caller-owned stack data remains valid until the dispatch ends. Do not launch a nested dispatch on the same pool.

## Persistent Worker

`PersistentWorker` is a long-lived single worker for subsystems that need explicit wake/wait control. Its caller contract is one `Wake` followed by `Wait`, with `Wait` completed before the next wake and before destruction. Calls come from one controlling thread. The type is neither copyable nor movable because its worker captures the owning object.

## Thread-Local State

Exactly one `ThreadLocal` may be active per thread. Construction installs the thread's log/workbuffer context, configures deterministic floating-point state, and optionally installs exception handling; destruction clears the thread-local pointer. The frame-tick scope marks code that must not call forbidden services during simulation.

Keep workbuffer views, log-buffer references, and tick/indent scopes within their owning thread and lifetime. DataPacker jobs that use Common scratch construct a `ThreadLocal` on their worker before export work.

## See Also

- `../Log/AGENTS.md` - worker log context and buffers

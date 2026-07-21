# Common - Shared Utilities and Data Formats

## Overview

`common::` is the foundation layer shared by DataPacker, Engine, Projects, and tools. It is independent of Engine and game layers, but may wrap platform or third-party facilities exposed through the approved aggregation headers. Add public headers to `Common.h`, the single Common aggregation header included by `Pch.h`.

Focused implementation contracts live in [Log](Log/AGENTS.md), [Math](Math/AGENTS.md), and [Threading](Threading/AGENTS.md). This hub owns only their cross-codebase usage rules.

## Shared Data Contracts

- **`DataFile.h`** defines `.pack` chunk headers, alignment, payload-offset helpers, texture-size metadata, and shared elevation thresholds. `DataHeader::kiVersion` incorporates `sizeof(ChunkHeader)`; payload sizes are also folded into owning export-job versions where applicable. Same-size layout or semantic changes still require the explicit version bump named by the nearby layout assertion.
- **Manifest integrity** keeps asset-path identity separate from the CRC of the exact emitted chunk bytes. Changes to layout or digest semantics require regeneration of local packed data.
- **`Serialization.h`** is the binary `Read`/`Write` surface, including `XMVECTOR` round-tripping through `XMFLOAT4`. Never hand-roll equivalent casts. Counts from saves, replays, network input, or pack chunks are untrusted: validate them with the provided count/capacity helpers before allocation or iteration, then let the owning boundary abort or skip corrupt input.
- **`Crc.h`** owns the repository's non-standard 64-bit CRC family and compile-time string helpers. Preserve its exact semantics wherever values feed deterministic state, manifests, or protocol identity.
- **`Flags<EnumType>`** is the serializable and CRC-hashable bitfield wrapper for unsigned enum types. Prefer it to parallel booleans or raw integer masks.
- **Texture sizing** uses `SizeInBytes` for one mip and `ComputeImageByteSize` for a complete mip chain. Do not duplicate the format/extent math in producers or consumers.

## Allocation-Free Scratch

Use `gpThreadLocal->mWorkbuffer` for transient buffers in tracked Engine/Game paths. `Push()` and `PushBuffer<T>()` return move-only RAII handles; keep all views inside the owning frame. Frames begin 16-byte aligned, so typed SIMD scratch is safe. Growth calls `DEBUG_BREAK()`: size the buffer before the hot path instead of relying on growth.

Allocation tracking is controlled by the global `ScopedSuppressAllocationTracking` guard. `ScopedResumeAllocationTracking` temporarily re-arms tracking inside an active suppression scope. Suppress only unavoidable, reviewed heap work and retain the root-required `// Heap:` rationale.

## Determinism and Math

- Simulation and CRC-fed math uses the deterministic helpers in [Math](Math/AGENTS.md), including the sanctioned RNG and transcendental replacements. Render-only paths may use non-deterministic library math.
- DirectXMath stays SSE4-only; AVX/AVX2 is rejected in `ExternalHeaders.h`. Every engine-managed thread configures MXCSR through `ThreadLocal`.
- `Determinism.h` owns exact IEEE equality for DirectXMath storage/vector types. Byte-identity diagnostics use byte comparison instead.
- `ValidateVector<IS_POSITION>` checks finite lanes and the repository W invariant at collection boundaries. Position W is `1.0f`; direction, velocity, normal, and offset W is `0.0f`.

## Logging and Threading Usage

- `LOG(category, level, format, ...)` is compile-time and runtime filtered. Allocation-tracked call sites must stay allocation-free; use the Common formatters/workbuffer wrappers rather than temporary strings. New formatters for Common-visible types belong in `Log/LogFormatters.h`; higher-layer formatters stay with their owning hub.
- `kTemp` is reserved for transient diagnostics. Durable messages use the owning category and the root log-level meanings.
- Use `gpMultithreading->Dispatch()` for bounded data-parallel work and `PersistentWorker` for a long-lived single worker. A pool dispatch is non-reentrant. Temporary allocations inside dispatched work use that worker's `ThreadLocal` workbuffer.

## Headers, Validation, and Platform

- PCH-backed standard-library and third-party consumption headers belong in `ExternalHeaders.h`, gated by the applicable build defines. The PCH-less AgentTools use `Tools/ToolCommon/ToolCliCommon.h`; third-party implementation units keep upstream implementation includes in `ThirdParty/Prebuilts/Source/`.
- Use `ASSERT`, `CHECK_HRESULT`, and `VERIFY_SUCCESS`; standard `assert` is deliberately unavailable. `DEBUG_BREAK()` triggers only with a debugger attached before the helper throws.
- `ThreadLocal` installs per-thread exception handlers and process-wide handlers through one-time initialization. Stack walking is serialized through the shared DbgHelp mutex; do not call DbgHelp independently. `ScopedExpectedThrows` suppresses designed C++-throw diagnostics, not structured-fault handling.
- `WindowsUtils.h` is the shared Win32 wrapper surface. Prefer its error, file-time, processor-topology, and child-process helpers over parallel wrappers.

## See Also

- [Log/AGENTS.md](Log/AGENTS.md) - filtering, buffers, formatting, and difference diagnostics
- [Math/AGENTS.md](Math/AGENTS.md) - deterministic algorithms, RNG, and convex hulls
- [Threading/AGENTS.md](Threading/AGENTS.md) - pool, persistent-worker, and thread-local lifecycle
- [Engine Memory](../Engine/Source/Memory/AGENTS.md) - allocator override and tracked-loop policy

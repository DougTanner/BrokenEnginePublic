<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T00:00:54.351Z","dependsOn":[]} -->
# Fix Release build: 64 KiB stack buffer in FileManager::ComputeOrdinaryFileSha256

## Context

Both `BrokenEngineSandbox` and `BrokenEngineSandboxServer` fail to build in `Release|x64`:

```
Engine\Source\File\FileManager.cpp(274): warning C6262: Function uses '65764' bytes of stack. Consider moving some data to heap.
Engine\Source\File\FileManager.cpp(321,1): error C2220: the following warning is treated as an error
```

The cause is the local `std::array<std::byte, 64 * 1024> buffer {};` at `Engine/Source/File/FileManager.cpp:296` inside `FileManager::ComputeOrdinaryFileSha256` (function starts at line 274). Release is the only configuration that runs `/analyze`, so `Profile|x64` builds succeed and the breakage stays invisible until a Release build runs.

Pre-existing at baseline `55beccf4`; introduced by commit `1c04c0df` "Add pack integrity handshake". Observed in this session's build envelopes:
`Temp/AgentBuildLogs/brokenenginesandbox-20260806T231805727Z-46760.log:55-56` and
`Temp/AgentBuildLogs/brokenenginesandboxserver-20260806T231938129Z-17564.log:68-69`.

## Design

Shrink the read chunk to `8 * 1024` bytes, keeping the existing streaming loop shape. The loop already re-reads until `ReadFile` returns zero bytes and hashes only `uiBytesRead` bytes, so chunk size is not part of any contract: the produced `FileContentDigest` (`sha256` and `iByteCount`) is byte-identical for every chunk size, and existing replay manifests keep authenticating.

A stack buffer is kept rather than moving to the heap or `gpThreadLocal->mWorkbuffer`. The two callers are in the replay artifact paths (`Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:234` and `:661`), which run inside the allocation-tracked main loop, so a heap allocation there would need `ScopedSuppressAllocationTracking`. A smaller stack chunk keeps the function allocation-free and removes the analyzer finding with the smallest possible edit. `8 * 1024` plus the roughly 228 bytes of other locals reported by the warning sits well under the analyzer's default 16 KiB stack threshold.

## Critical files

- `Engine/Source/File/FileManager.cpp` — `FileManager::ComputeOrdinaryFileSha256` (line 274; buffer declaration at line 296)

## In scope

- The `buffer` declaration inside `FileManager::ComputeOrdinaryFileSha256`, and any comment on that declaration that names its size

## Out of scope

- The hashing algorithm, `Sha256Hasher`, `FileContentDigest`, and the digest values written to replay manifests
- The callers in `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp`
- Any other C6262 or `/analyze` finding elsewhere in the tree
- Changing analyzer settings, warning levels, or warnings-as-errors configuration

## Risk tier and invariants

Expected Change Workflow Tier 2 — one subsystem's runtime behavior (Engine file I/O) in a path that feeds replay manifest authentication. Replay and save compatibility are not exposed, because the digest is independent of chunk size; escalate to Tier 3 only if the fix stops producing identical digests. No determinism/CRC, wire, or threading surface is touched.

## Acceptance criteria

- `Release|x64` builds of both `BrokenEngineSandbox` and `BrokenEngineSandboxServer` complete with no C6262 or C2220 in `FileManager.cpp`
- `Profile|x64` builds of both projects still succeed
- The function still allocates no heap memory (no `ScopedSuppressAllocationTracking` added)

## Notes

Only Release runs `/analyze`, so this class of breakage needs a Release build to detect. This Plan does not add one to any routine flow.

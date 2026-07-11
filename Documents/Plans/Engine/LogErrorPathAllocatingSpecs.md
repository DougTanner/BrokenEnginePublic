# Log Error-Path Allocating Format Specs

## Context

Six pre-existing `LOG(...)` calls on `kError`/`kWarning` paths use allocating `std::format` specs (`{:#018x}`, `0x{:08X}`). Under the allocation-tracked Game/Engine builds these trip the allocation tracker (`DEBUG_BREAK()`) — and they fire exactly when a real corrupt-asset or audio error needs logging, so the diagnostic path breaks precisely when it is most needed. The root-AGENTS.md LOG-formatting rule requires the placeholder stay `{}` and hex/float wrapping go through allocation-free helpers.

The six sites (verified 2026-07-09):

- `Engine/Source/Graphics/AnimationData.cpp:496` — `Corrupt animation data for GLTF CRC {:#018x}: {}`
- `Engine/Source/Graphics/Managers/TextManager.cpp:35` — `Corrupt font chunk {:#018x}: implausible character count {}`
- `Engine/Source/Graphics/Managers/PipelineManager.cpp:52` — `Corrupt shader chunk {:#018x}: implausible binding/attribute counts {} / {}`
- `Engine/Source/Graphics/Managers/PipelineManager.cpp:64` — `Corrupt shader chunk {:#018x}: section extent exceeds chunk bytes`
- `Engine/Source/Graphics/Managers/DynamicPipelines.cpp:50` — `Corrupt scene chunk ... (scene CRC {:#018x}): {}`
- `Engine/Source/Audio/StreamingVoices.cpp:218` — `SubmitSourceBuffer failed, HRESULT: 0x{:08X}`

## Design

Mechanical `{}`-conversion mirroring the allocation-free hex sites converted elsewhere this session. Route each hex value through an allocation-free helper so the placeholder stays `{}`:

- Replace `{:#018x}` / `0x{:08X}` with a plain `{}` fed by an allocation-free zero-alloc hex formatter (e.g. `common::ToHex` from `StringUtils.h`, or a small stack-buffer `to_chars`-based hex wrapper), preserving the existing width/`0x` prefix in the produced text.
- Preserve message wording and the remaining `{}` args byte-for-byte; only the hex spec + its argument wrapping change.

Confirm the chosen helper is allocation-free and visible in each TU (`StringUtils.h` is Common-layer). If no existing helper produces the exact `0x`-prefixed fixed-width form, add one small allocation-free formatter wrapper rather than hand-rolling per site.

## Critical files

- `Engine/Source/Graphics/AnimationData.cpp` (`:496`)
- `Engine/Source/Graphics/Managers/TextManager.cpp` (`:35`)
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` (`:52`, `:64`)
- `Engine/Source/Graphics/Managers/DynamicPipelines.cpp` (`:50`)
- `Engine/Source/Audio/StreamingVoices.cpp` (`:218`)
- Possibly `Common/StringUtils.h` / `Common/Log/LogFormatters.h` if a new allocation-free hex wrapper is added.

## Out of scope

- Any change to log wording, severity, or category.
- Non-hex allocating specs elsewhere (float `{:.Nf}`, width/precision) — not part of this batch.
- CRC / wire / `.pack` layout / determinism: none of these are CRC-fed or serialized; the hex values are already-computed CRCs/HRESULTs printed only for diagnostics. No `kiVersion` bump, no wire exposure.

## Notes

- Purely a diagnostics-path correctness fix; no runtime behavior change on the success path.
- State invariant exposure: none — no determinism/CRC/`.pack`/replay/client-server-guard/serialization surface touched.

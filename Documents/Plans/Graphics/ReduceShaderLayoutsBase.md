<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-04T02:59:57.293Z","dependsOn":[]} -->
# Reduce ShaderLayoutsBase header by splitting uniform blocks

## Context

The GLSL review recorded a qualifying pre-existing size residual: `Engine/Data/Shaders/ShaderLayoutsBase.h` measures 8,239 `bt-token-v1` tokens (872 lines, 32,954 bytes), above the 5,000-token header threshold. The active Water change did not authorize a header-size reduction, and the residual is independently landable after that change.

This is a dual-language header. `BT_ENGINE` selects C++ versus GLSL declarations, while `Projects/BrokenEngineSandbox/Data/Shaders/ShaderLayouts.h` is the sole public wrapper included by the C++ PCH and by the DataPacker's shader sources. Both client and server projects list the base header. The two uniform-block structs dominate the file: `GlobalLayout` occupies the current lines 197-533 (3,998 `bt-token-v1` tokens) and `MainLayout` occupies lines 534-689 (1,541 tokens). Their field names, order, scalar types, and alignment are already a shared CPU/GLSL contract.

## Design

Split the two existing uniform-block definitions into two internal dual-language fragments without changing their declarations or their inclusion seam:

1. Create `Engine/Data/Shaders/ShaderGlobalLayout.h` containing the exact `GlobalLayout` struct body. It is an internal fragment included only by `ShaderLayoutsBase.h` after the existing language macros, scalar type shims, constants, and `PushConstantsLayout` are established. Use a unique preprocessor include guard; do not reopen or close the `shaders` namespace in the fragment.
2. Create `Engine/Data/Shaders/ShaderMainLayout.h` containing the exact `MainLayout` struct body. It follows the Global fragment at the original declaration position, uses the same internal-fragment guard convention, and relies only on declarations already retained in `ShaderLayoutsBase.h`.
3. In `ShaderLayoutsBase.h`, replace the two inline struct bodies with includes in the same order. Retain the preamble, shared constants, `PushConstantsLayout`, all smaller payload structs, the closing namespace, and the `sizeof(GlobalLayout) <= 65536` assertion in the base header. `ShaderLayouts.h` and every existing shader include remain unchanged.
4. Add both new headers to the client and server Visual Studio project/filter membership through `/update-vcxproj`. They are shared `ClInclude` headers; no shader `<None>` source entry or build-setting change is needed.

The split is intentionally by the existing global and main uniform-block responsibilities. No field is renamed, reordered, retyped, nested, or moved between blocks, so the CPU/GLSL layout ABI remains byte-identical.

## Critical files

- `Engine/Data/Shaders/ShaderLayoutsBase.h` — retained preamble/constants and include sites replacing the `GlobalLayout` and `MainLayout` bodies; preserve namespace, macro, field-order, and size-assert contracts.
- `Engine/Data/Shaders/ShaderGlobalLayout.h` — new exact `GlobalLayout` dual-language fragment.
- `Engine/Data/Shaders/ShaderMainLayout.h` — new exact `MainLayout` dual-language fragment.
- `Projects/BrokenEngineSandbox/Data/Shaders/ShaderLayouts.h` — read-only public wrapper and include-seam verification site.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` and `.filters` — client `ClInclude` membership for both headers.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandboxServer.vcxproj` and `.filters` — server `ClInclude` membership for both headers.
- `Engine/Data/Shaders/AGENTS.md` and `Projects/BrokenEngineSandbox/Data/Shaders/AGENTS.md` — read-only dual-language, include-seam, and client/server contract guidance.

## In scope

- `ShaderLayoutsBase.h`: the `GlobalLayout` and `MainLayout` struct regions, their two replacement includes, and no other layout definitions.
- New `ShaderGlobalLayout.h` and `ShaderMainLayout.h`: exact moved struct bodies plus the internal-fragment include guards required to prevent duplicate declarations.
- Client/server project and filter membership for the two new shared headers, validated through `/update-vcxproj`.
- Static identity checks that the two moved structs retain their field names, order, scalar types, and C++/GLSL `INIT` usage, and that all existing consumers still enter through `ShaderLayouts.h`.

## Out of scope

- Any field rename, reorder, retype, nesting, padding, uniform binding, descriptor, or `sizeof(GlobalLayout)` change.
- Changes to `Projects/BrokenEngineSandbox/Data/Shaders/ShaderLayouts.h`, shader source behavior, DataPacker source, generated `.pack` data, or C++/GLSL callers.
- Extraction of the smaller payload structs, constants, or type shims; unrelated file-size cleanup; style-only edits; and unit tests.
- Simulation, CRC, serialization, save/replay, wire/protocol, `kiVersion`, threading, allocation, or runtime behavior changes.

## Risk tier and invariants

Tier 3 — shared CPU/GLSL layout and build-integration exposure. Although the reduction is behavior-preserving, the header is compiled into both client and server and is consumed by DataPacker shader compilation. `GlobalLayout` and `MainLayout` must remain byte-identical in field order, scalar type, namespace, and alignment; `sizeof(GlobalLayout) <= 65536` remains enforced; the public wrapper and every existing shader include path remain unchanged. No simulation/CRC, persistence, wire, or runtime state is introduced.

## Acceptance criteria

- `Measure-Tokens.ps1` reports `ShaderLayoutsBase.h` at or below 5,000 `bt-token-v1` tokens after the split, and both new headers are individually at or below the same header threshold.
- Expected post-split sizes are approximately `ShaderLayoutsBase.h` at 2,700 `bt-token-v1` tokens (from 8,239), `ShaderGlobalLayout.h` at 3,998, and `ShaderMainLayout.h` at 1,541; each remains below the 5,000-token threshold.
- A structural comparison of the pre-split and post-split `GlobalLayout` and `MainLayout` declarations shows identical field names, order, scalar types, and array extents; the base header retains the size assertion and original declaration order.
- DataPacker shader repack succeeds, and `BrokenEngineSandbox` client and `BrokenEngineSandboxServer` Debug|x64 builds compile/link with the shared fragments through the existing wrapper.
- `/update-vcxproj` confirms both new headers are present in client and server `ClInclude` groups and matching filters, with no unrelated project membership changes.
- Repository search finds no direct shader or C++ include of either internal fragment outside `ShaderLayoutsBase.h`; the base header is the sole direct include site, existing consumers continue to include `ShaderLayouts.h`, and no generated, serialization, or runtime artifacts change.

## Notes

This Plan records the pre-existing GLSL review size observation only. It is independent of the Water behavior Plan and must be implemented against the then-current `ShaderLayoutsBase.h`; if another layout-semantic Plan is concurrently changing the same header, rebase and re-audit the moved declarations before implementation rather than combining semantic edits with this reduction.

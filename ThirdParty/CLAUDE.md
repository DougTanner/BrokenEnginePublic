# ThirdParty

External libraries, kept upstream-pristine — never edit library source; all adaptation (configuration `#define`s, warning suppression, config-shim headers) lives in the `Prebuilts/Source/` wrappers. Adding a *new* library requires explicit user approval and must satisfy the License Policy below.

## Build Organization

Everything compiles into a single `ThirdParty.<Config>.lib` linked by client, server, and DataPacker. Most compiled libraries are wrapped by a unity-build `.cpp` in `Prebuilts/Source/Engine/` (runtime consumers) or `Prebuilts/Source/DataPacker/` (offline asset tooling) that `#include`s the upstream sources; bc7enc_rdo and zlib compile directly from their upstream trees. See [Prebuilts/Platforms/VisualStudio2026/CLAUDE.md](Prebuilts/Platforms/VisualStudio2026/CLAUDE.md) for build config and how to register a new unit. Header-only with no compilation unit: glm, gli, PerlinNoise, RenderDoc.

Headers consumed by Common/Engine go through `Common/ExternalHeaders.h` (with `BT_CLIENT`/`BT_ENGINE` gating); DataPacker-only headers are instead included locally in the relevant DataPacker `.cpp` with their own warning-suppression pushes — a sanctioned exception keeping offline-only headers out of the engine PCH.

## Library Inventory

**Engine / runtime**
- **DirectXTK** (MIT) — client audio, GamePad/Mouse input (XINPUT, for Steam Deck compatibility), DataPacker WAV parsing. DirectXMath itself comes from the Windows SDK, not from here. `Keyboard` is deliberately not compiled — evaluated and rejected; the in-house Raw Input path's `RIDEV_NOLEGACY` semantics are load-bearing (see `Engine/Source/Input/CLAUDE.md`).
- **imgui** + **implot** (MIT) — debug UI and plots; implot compiles inside the imgui unity unit
- **enet** (MIT) — UDP networking
- **lz4** (BSD-2; see caveat) — network payload compression
- **mimalloc** (MIT) — allocator behind the global `operator new`/`delete` overrides (and allocation tracking) in `Engine/Source/Memory/GlobalAllocator.cpp`
- **Clipper2** (Boost) — 2D polygon clipping for navmesh build; runs on the shared sim path — chosen for int64-quantized determinism
- **StackWalker** (BSD-2) — crash stack capture; use via `common::FilteredStackWalker`, never directly (DbgHelp is process-single-threaded)
- **zlib** (zlib) — runtime `.pack` chunk decompression in FileManager (client and server), DataPacker intermediate compression, deflate backend for openexr
- **stb** (MIT / public domain) — image write (engine screenshots); image load/resize/write (DataPacker). The shared write implementation defines `STBIW_WINDOWS_UTF8`, so every `stbi_write_*` caller passes filenames as UTF-8 (e.g. `std::filesystem::path::u8string()`), never ANSI
- **PerlinNoise** (MIT) — header-only noise (client render path)
- **RenderDoc** (`renderdoc_app.h`, MIT) — in-app capture API header

> Vma (Vulkan Memory Allocator) and Volk (Vulkan loader) also have unity units in `Prebuilts/Source/Engine/`; their headers come from the Vulkan SDK (`VK_SDK_PATH`), not a folder here. Same precedent: `vulkan/vk_enum_string_helper.h` (Apache-2.0, generated enum-to-string helpers from Vulkan-Utility-Libraries, ships with the LunarG SDK) — header-only, consumed via `Common/ExternalHeaders.h` in all three builds.

**DataPacker / offline tooling**
- **bc7enc_rdo** (MIT / public domain; bc7e.ispc Apache-2.0, lodepng zlib) — BCn texture compression; the bundled bc7e.ispc path is deliberately disabled — the scalar C++ encoder keeps bakes reproducible
- **cmft** (BSD-2) — cubemap filtering / IBL
- **meshoptimizer** (MIT) — mesh optimization
- **openexr** (BSD-3) — HDR `.exr` reads; OpenEXRCore C API only, with config-shim headers under `Prebuilts/Source/DataPacker/openexr/` replacing its CMake configure step
- **SPIRV-Cross** (Apache-2.0) — shader reflection (descriptor and vertex-input layouts baked into `.pack`)
- **tinygltf** (MIT) — glTF model/animation import
- **tinyobjloader** (MIT) — compiled but currently has no call sites
- **glm** / **gli** (MIT / Happy Bunny dual — use the MIT terms) — gli handles DDS/KTX containers; glm is vendored only so gli's `<glm/...>` includes resolve — no first-party code uses it directly

## License Policy

Only permissive open-source licenses that allow closed-source commercial redistribution. Acceptable:

- MIT / X11
- BSD-2-Clause, BSD-3-Clause
- Apache-2.0
- zlib / libpng
- Boost Software License 1.0
- ISC
- Unlicense / public domain / CC0

No viral (copyleft) licenses. Reject any library distributed under:

- GPL (any version) — strong copyleft
- AGPL — strong copyleft, extends to network use
- LGPL — weak copyleft (still impractical for static-linked engine code)
- MPL, EPL, CDDL — file-level copyleft
- SSPL, "source-available", or non-commercial-only licenses

When adding a new library:

1. Read its `LICENSE` / `COPYING` *before* importing.
2. If multi-licensed (see lz4 caveat below), import only the permissive parts. Never add copyleft-licensed files to a `.vcxproj`.

## lz4 caveat

`lz4` ships under two licenses:

- `lz4/lib/**` — BSD-2-Clause (the only portion linked into the engine)
- `lz4/programs/**`, `lz4/tests/**`, `lz4/examples/**` — GPL-2.0-or-later (the standalone CLI utility and benchmarks)

Never reference files outside `lz4/lib/` from any `.vcxproj`.

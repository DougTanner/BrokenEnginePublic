# ThirdParty

External libraries. **DO NOT modify** the source code of existing libraries — keep upstream pristine so updates can be re-imported cleanly.

Adding a *new* library is allowed, but only with **explicit user approval** and only if it satisfies the License Policy below.

## Build Organization

Compiled libraries are built into a single `ThirdParty.lib` static library linked by both Engine and DataPacker. Each compiled library is wrapped by a unity-build `.cpp` in `Prebuilts/Source/Engine/` (client/runtime) or `Prebuilts/Source/DataPacker/` (offline asset tooling) that `#include`s the upstream sources. See [Prebuilts/Platforms/VisualStudio2026/CLAUDE.md](Prebuilts/Platforms/VisualStudio2026/CLAUDE.md) for build config and how to register a new unity unit. Header-only libraries (glm, gli, stb, tinygltf, tinyobjloader, PerlinNoise, RenderDoc, implot) are included directly with no compilation unit.

## Library Inventory

**Engine / runtime**
- **DirectXTK** (MIT) — math, audio, helpers
- **imgui** + **implot** (MIT) — debug UI and plots
- **enet** (MIT) — UDP networking
- **lz4** (BSD-2; see caveat) — fast compression
- **mimalloc** (MIT) — allocator
- **Clipper2** (Boost) — 2D polygon clipping
- **StackWalker** (BSD-2) — crash stack capture
- **stb** (MIT / public domain) — image, font, misc single-header utilities
- **PerlinNoise** (MIT) — header-only noise
- **glm** / **gli** (MIT / Happy Bunny dual — use the MIT terms) — header-only math / texture loading
- **RenderDoc** (`renderdoc_app.h`, MIT) — in-app capture API header

> Vma (Vulkan Memory Allocator) and Volk (Vulkan loader) also have unity units in `Prebuilts/Source/Engine/`; their headers come from the Vulkan SDK (`VK_SDK_PATH`), not a folder here.

**DataPacker / offline tooling**
- **bc7enc_rdo** (MIT / public domain; bc7e.ispc Apache-2.0, lodepng zlib) — BC7/BCn texture compression
- **cmft** (BSD-2) — cubemap filtering / IBL
- **meshoptimizer** (MIT) — mesh optimization
- **openexr** (BSD-3) + **zlib** (zlib) — HDR image and deflate codecs
- **SPIRV-Cross** (Apache-2.0) — shader reflection / cross-compile
- **tinygltf** / **tinyobjloader** (MIT) — model loaders

## License Policy

Only **permissive open-source licenses** that allow closed-source commercial redistribution. Acceptable:

- MIT / X11
- BSD-2-Clause, BSD-3-Clause
- Apache-2.0
- zlib / libpng
- Boost Software License 1.0
- ISC
- Unlicense / public domain / CC0

**NO viral (copyleft) licenses.** Reject any library distributed under:

- GPL (any version) — strong copyleft
- AGPL — strong copyleft, extends to network use
- LGPL — weak copyleft (still impractical for static-linked engine code)
- MPL, EPL, CDDL — file-level copyleft
- SSPL, "source-available", or non-commercial-only licenses

When adding a new library:

1. Read its `LICENSE` / `COPYING` *before* importing.
2. If multi-licensed (see lz4 caveat below), import only the permissive parts. **Never add copyleft-licensed files to a `.vcxproj`.**

## lz4 caveat

`lz4` ships under two licenses:

- `lz4/lib/**` — **BSD-2-Clause** (the only portion linked into the engine)
- `lz4/programs/**`, `lz4/tests/**`, `lz4/examples/**` — **GPL-2.0-or-later** (the standalone CLI utility and benchmarks)

Never reference files outside `lz4/lib/` from any `.vcxproj`.

# ThirdParty

Vendored external libraries. Upstream source stays pristine: never edit files inside an external library tree. Adding a library requires explicit user approval and license review.

## Adaptation and Consumption

- Implementation adaptation belongs in `Prebuilts/Source/` wrappers: unity units, generated configuration shims, implementation-only defines, and source-level warning boundaries.
- PCH-backed consumption defines, suppressions, and includes belong in `Common/ExternalHeaders.h`, gated by `BT_CLIENT`, `BT_ENGINE`, `BT_SERVER`, or `BT_DATA_PACKER` as appropriate.
- PCH-less AgentTools centralize shared consumption in `Tools/ToolCommon/ToolCliCommon.h`.
- Outside those aggregation surfaces, third-party headers appear only in implementation wrappers that must include upstream sources directly.

Compiled units link through the shared ThirdParty static library. [Prebuilts/Platforms/VisualStudio2026/AGENTS.md](Prebuilts/Platforms/VisualStudio2026/AGENTS.md) owns configuration, output naming, project registration, and the deliberate client/server/DataPacker exclusions.

Runtime wrappers live mainly under `Prebuilts/Source/Engine/`; offline asset wrappers live under `Prebuilts/Source/DataPacker/`. Some libraries compile directly from a permitted upstream subtree when their build does not use a unity wrapper. Preserve existing deterministic choices such as the scalar BC encoder and integer-quantized Clipper2 path.

## License Policy

Only permissive licenses compatible with closed-source commercial redistribution are accepted: MIT/X11, BSD-2/3-Clause, Apache-2.0, zlib/libpng, Boost 1.0, ISC, Unlicense, public domain, and CC0.

Reject GPL, AGPL, LGPL, MPL, EPL, CDDL, SSPL, source-available, and non-commercial-only terms. For a multi-licensed dependency, import and compile only files covered by the approved permissive license.

Before importing or registering a library:

1. Read its `LICENSE` or `COPYING` file.
2. Confirm every compiled file is under accepted terms.
3. Record required notices and attribution with the repository's distributed licenses.
4. Obtain explicit user approval before adding the dependency.

`lz4/lib/**` is BSD-2-Clause and may be linked. Do not reference its GPL-covered `programs`, `tests`, or `examples` trees from any project.

## Boundaries

Do not add `AGENTS.md` or `CLAUDE.md` inside external submodule folders. This file owns the upstream and license boundary; the existing Prebuilts platform document owns build registration.

# ThirdParty

Vendored external libraries. Upstream source stays pristine: never edit files inside an external library tree. Adding a library requires explicit user approval and license review.

## Adaptation and Consumption

- Implementation adaptation belongs in `Prebuilts/Source/` wrappers: unity units, generated configuration shims, implementation-only defines, and source-level warning boundaries.
- PCH-backed consumption defines, suppressions, and includes belong in `Common/ExternalHeaders.h`, gated by `BT_CLIENT`, `BT_ENGINE`, `BT_SERVER`, or `BT_DATA_PACKER` as appropriate.
- PCH-less AgentTools centralize shared consumption in `Tools/ToolCommon/ToolCliCommon.h`.
- Outside those two headers that gather each library's includes, third-party headers appear only in implementation wrappers that must include upstream sources directly.

Compiled units link through the shared ThirdParty static library. `Prebuilts/Platforms/VisualStudio2026/AGENTS.md` owns build configuration, output naming, and project registration.

Runtime wrappers live mainly under `Prebuilts/Source/Engine/`; offline asset wrappers live under `Prebuilts/Source/DataPacker/`. Some libraries compile directly from a permitted upstream subtree when their build does not use a unity wrapper. Preserve existing deterministic choices such as the scalar BC encoder and integer-quantized Clipper2 path.

Leaving one of a library's files out of a wrapper is a decision, not an oversight. DirectXTK's `Keyboard.cpp` is deliberately not compiled: our own Raw Input path owns keyboard state and registers the keyboard with `RIDEV_NOLEGACY` (a Windows flag that stops the old-style keyboard messages from being delivered at all). Compiling it would add a second, half-fed source of keyboard state, let the debug UI swallow key presses, and break bindings that read the generic Alt/Shift/Ctrl virtual keys — the Alt+F4 quit binding among them — because DirectXTK reports only the left/right-specific keys. Do not unify the keyboard onto DirectXTK to match the mouse and gamepad paths; see `Engine/Source/Input/AGENTS.md`.

## License Policy

Only permissive licenses compatible with closed-source commercial redistribution are accepted: MIT/X11, BSD-2/3-Clause, Apache-2.0, zlib/libpng, Boost 1.0, ISC, Unlicense, public domain, and CC0.

Reject GPL, AGPL, LGPL, MPL, EPL, CDDL, SSPL, source-available, and non-commercial-only terms. For a multi-licensed dependency, import and compile only files covered by the approved permissive license.

Before importing or registering a library:

1. Read its `LICENSE` or `COPYING` file.
2. Confirm every compiled file is under accepted terms.
3. Record required notices and attribution with the repository's distributed licenses.
4. Obtain explicit user approval before adding the dependency.

`scb-check` is an upstream submodule. Preserve its upstream Apache-2.0 `LICENSE`; its pinned `pyproject.toml` declares `license = "MIT"`. These are separate source facts, not a synthesized license conclusion.

`lz4/lib/**` is BSD-2-Clause and may be linked. Do not reference its GPL-covered `programs`, `tests`, or `examples` trees from any project.

## Boundaries

Do not add or edit `AGENTS.md` or `CLAUDE.md` inside external library or submodule folders. Upstream-owned copies are pristine third-party data and do not override Broken Engine policy. This file owns the upstream and license boundary; the existing Prebuilts platform document owns build registration.

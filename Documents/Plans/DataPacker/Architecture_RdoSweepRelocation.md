# Architecture: RDO Sweep Relocation

## Context
Source: /external-architecture-review on `DataPacker/Source/`. The ~250-line RDO sweep diagnostic block in `Main.cpp` (`RdoSweepResult`/`RunRdoSweepOne`/`RunRdoSweep`/`RunRdoSweepFull`/`RunRdoSweepValidate`/`LoadBc7AsFloatPixelsMip0`, `Main.cpp:67-315`) leaks the `Texture` module's internals into the orchestrator TU: it takes `Texture::sEncodeMutex` directly at `Main.cpp:109/146/293` (a caller-must-hold contract documented inside `Texture.cpp`) and includes ThirdParty `bc7enc_rdo/bc7decomp.h` directly (`Main.cpp:17-27`), bypassing the `Texture` module that otherwise encapsulates the BC7 encoder. Moving the block next to `Texture` confines both the mutex knowledge and the ThirdParty include, and reduces `Main.cpp` from 688 to ~440 lines.

## Design

### New: DataPacker/Source/ExportJobs/Texture/RdoSweep.h / RdoSweep.cpp
- Move `RdoSweepResult`, `RunRdoSweepOne`, `RunRdoSweep`, `RunRdoSweepFull`, `RunRdoSweepValidate`, and `LoadBc7AsFloatPixelsMip0` (`Main.cpp:67-315`) into a new TU beside `Texture.{h,cpp}`. Header exposes only the three CLI entry points (`RunRdoSweep`, `RunRdoSweepFull`, `RunRdoSweepValidate`); everything else becomes file-static. The `bc7decomp.h` include (with its warning-suppression block) and the `Texture::sEncodeMutex` lock sites move with it. [~30m]

### DataPacker/Source/Main.cpp
- Replace the moved block with `#include "ExportJobs/Texture/RdoSweep.h"`; the `--rdo-sweep*` dispatch in `main` (`Main.cpp:600-620`) is unchanged. Drop the now-unneeded `bc7decomp.h` include block (`Main.cpp:17-27`). [~10m]

### DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj (+ .filters)
- Add `RdoSweep.h`/`RdoSweep.cpp` to the project and the `ExportJobs/Texture` filter. [~5m]

## Critical files
- `DataPacker/Source/Main.cpp`
- `DataPacker/Source/ExportJobs/Texture/RdoSweep.h` (new)
- `DataPacker/Source/ExportJobs/Texture/RdoSweep.cpp` (new)
- `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj` (+ filters)

## Out of scope
- Making `sEncodeMutex` private to `Texture` — `MigrateLegacyIntermediates.cpp` also locks it; narrowing the contract is a `Texture`-module decision, not this move.
- Decomposing the rest of `Main.cpp` (generated-header writers, CRT-debug allocator overrides, process plumbing) — cohesive enough where it is; `Main.cpp` is under the `/reduce-file` threshold after the move.
- Any change to sweep behavior, knobs, or the intermediate format read by `LoadBc7AsFloatPixelsMip0`.
- DataPacker/Source/CLAUDE.md mention of the three CLI modes stays accurate (they still exist; only their TU changes) — touch only if the doc names `Main.cpp` explicitly.

## Notes
- Pure code motion — no behavior change, no determinism/CRC exposure. Compile-checked.
- Execute after the other `Main.cpp` plans in this batch (`Architecture_OutputWriteTrustBoundary`, `Architecture_PackStalenessAndCrcGuards`, `Refactor_MainQuickWinMechanics`) — this move shifts ~250 lines and relocates `LoadBc7AsFloatPixelsMip0`, whose hardening item lives in the trust-boundary plan.

## Verification Notes
- **No hidden move blockers — verified by reading `Main.cpp:67-315` line by line.** Everything the moved block references resolves outside `Main.cpp`: `Texture` / `FileType::kImage` / `TextureOptions_t` / `kiTextureIntermediateMagic` (`Texture.h:3/26`), public `Texture::sEncodeMutex` (`Texture.h:35`) and `Texture::EncodeWithRdo` (`Texture.h:76`), zlib (`compressBound`/`compress2`/`uncompress` — already reach `Main.cpp` via the PCH, no local include today), `bc7decomp.h` (include block moves with the code), Vulkan formats / `LOG` / `ASSERT` via PCH. The block references **no** `Main.cpp` statics (`kDataTypes`, `WriteIfChanged`, `kDataTypeCount`) and never calls `Quit` — failures inside the sweep throw and hit `main`'s top-level handler unchanged.
- `Texture::sEncodeMutex` lock sites at `Main.cpp:109/146/293` confirmed; `MigrateLegacyIntermediates.cpp:220` also locks it (as do `ExportIsland.cpp`, `ExportScene.cpp`, `ExportTexture.cpp`), so the out-of-scope call on narrowing the mutex is correct — even more callers than the plan names.
- DataPacker `.cpp` files do not `#include "Pch.h"` explicitly (force-included via the vcxproj) — the new `RdoSweep.cpp` follows the same pattern; no PCH line needed.
- `DataPacker/Source/CLAUDE.md`'s three-CLI-modes paragraph does not name `Main.cpp` for the sweep functions — per the plan's own out-of-scope rule, no doc edit is required.
- **Cross-plan interaction**: `Common/StaleCodeCommentsSweep.md` (Order.md row 7) edits the stale "current production knobs" comment at `Main.cpp:305`, which this move relocates into `RdoSweep.cpp` — sequence the two or rely on `/next-plan`'s citation refresh.

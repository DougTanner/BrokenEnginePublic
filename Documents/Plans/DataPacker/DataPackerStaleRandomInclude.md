<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-01T21:20:46.576Z","dependsOn":[]} -->
# Fix the Stale RandomManager.h Include in the DataPacker Project Files

## Context

Found while verifying the completed `FleetRandomStateLoadValidation` change; entirely pre-existing and unrelated to it.

The DataPacker project declares a header that does not exist:

- `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj:72` — `<ClInclude Include="..\..\..\Common\RandomManager.h" />`
- `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj.filters:206-208` — the same path under `<Filter>Common</Filter>`

`Common/RandomManager.h` is absent from the working tree and `git log --all -- Common/RandomManager.h` returns nothing, so no rename is recorded. A scan of every `ClInclude` in that project file found this as the only missing one.

The matching implementation file *is* a member: `DataPacker.vcxproj:24` compiles `..\..\..\Common\Math\Random.cpp` (filters at `:144-146`, filter `Common\Math`), but its header `Common/Math/Random.h` is not listed anywhere in the DataPacker project. The sibling projects list both halves — `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj:324` and `:519`, and `BrokenEngineSandboxServer.vcxproj:322` and `:452`, each with the header under filter `Common\Math` in their `.filters`. So the DataPacker entry is a leftover of the header's move into `Common/Math/Random.h`, and the correct state is the one the two sandbox projects already use.

A stale `ClInclude` does not fail the build — MSBuild treats it as IDE metadata only — so this shows up as a missing file in Solution Explorer and as project membership that disagrees with the tree, not as a compile error.

## Design

Repoint both entries at the real header instead of deleting them, matching the two sandbox projects.

1. In `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj`, replace the `:72` entry with `<ClInclude Include="..\..\..\Common\Math\Random.h" />`, keeping it in the same `ItemGroup` and in the position the surrounding ordering implies.
2. In `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj.filters`, replace the `:206-208` entry's path the same way and set its `<Filter>` to `Common\Math`, matching the sibling `Common\Math\MathUtils.h` and `Common\Math\ConvexHull.h` entries at `:200-205` and the `Random.cpp` filter at `:144-146`.
3. Change nothing else in either file: no other membership, no configuration, no property, no ordering rewrite.

Decision already made, do not re-open: repoint rather than delete. `Random.cpp` is compiled by this project, so its header belongs to the project the same way it does in both sandbox projects; deleting the entry would leave the header out of DataPacker's IDE membership.

Run `/update-vcxproj` after the edit — this is a file-membership change, which is exactly what that skill validates.

## Critical files

- `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj` — the `ClInclude` at `:72`; edited.
- `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj.filters` — the matching entry at `:206-208`; edited.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` (`:324`, `:519`) and `BrokenEngineSandboxServer.vcxproj` (`:322`, `:452`) plus their `.filters` — read-only, the convention being matched.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md` — the owning membership and filter rules; read-only.

## In scope

- `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj` — replace the single `ClInclude` for `..\..\..\Common\RandomManager.h` at `:72` with `..\..\..\Common\Math\Random.h`.
- `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj.filters` — replace the matching `ClInclude` path at `:206-208` and set its `<Filter>` to `Common\Math`.

## Out of scope

- Any other `ClInclude`, `ClCompile`, or `None` entry in either DataPacker file, and any other project or filters file in the repository.
- Build configurations, properties, toolset settings, PCH settings, and solution files.
- Creating, moving, renaming, or deleting any source or header file.
- The sandbox client/server projects, which already carry the correct entries.

## Risk tier and invariants

Change Workflow Tier 1 — trigger: project membership only, no public signature or invariant exposure. Membership must stay consistent with the `AGENTS.md` rules for `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026`, and the `.vcxproj` and `.filters` pair must agree. No determinism/CRC, wire, serialization, save/replay, threading, allocation, or shader exposure. Because a stale `ClInclude` never failed the build, a passing build is not by itself evidence — the file-existence and pairing checks are.

## Acceptance criteria

- Neither DataPacker project file contains `RandomManager.h`, and both contain `..\..\..\Common\Math\Random.h`; the filters entry sits under `Common\Math`.
- Every `ClInclude` path in `DataPacker.vcxproj` resolves to a file that exists in the working tree.
- `/update-vcxproj` reports the DataPacker project and filters as consistent.
- The DataPacker build still succeeds.

## Notes

- Line citations are tip-of-2026-08-01; refresh them if either project file is edited first.

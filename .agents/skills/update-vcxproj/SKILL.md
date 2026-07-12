---
name: update-vcxproj
description: >-
  Adds or verifies source-file membership in .vcxproj and .vcxproj.filters
  project files (BrokenEngineSandbox client/server, DataPacker) — client/server
  affinity from BT_CLIENT/BT_SERVER guard scope, filter paths mirroring on-disk
  directories, new-filter GUIDs, shader <None> items. Use whenever new
  .cpp/.h/shader files are created, when a file gained or lost a file-wide BT_
  guard (affinity change), when verifying vcxproj inclusion for a changed-file
  list, or when the compiler/linker can't find code that exists on disk.
  Designed for a cheap subagent — the project XML is huge; work via Grep
  anchors, report per-file results, never echo the XML.
allowed-tools: [Read, Edit, Grep, Glob]
---

# Update vcxproj Membership

Add new files to, or verify existing files against, the MSBuild project files. Two modes — infer from the invocation:

- **Add** — wire the given files into the correct project(s) and filters.
- **Verify** — check each given file's membership and filter path; report pass/fail without editing unless asked to fix.

For C++ Code Change Process step 7, verify every file changed during the session, fix each `FAIL` through Add mode, then reverify it. This skill owns project membership and filter mechanics; callers do not hand-edit project XML.

## Rules

Read `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md` first — the authoritative rule source (inclusion rules, filter-path mirroring, affinity naming conventions, shader handling). Orientation summary:

- Affinity follows preprocessor-guard scope: shared files (no or partial `BT_CLIENT`/`BT_SERVER` guards) go in **both** game projects; fully wrapped files go **only** in the matching project — and must be removed from the opposite one if the guard scope changed.
- Game projects: `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` (client) and `BrokenEngineSandboxServer.vcxproj`, each with a sibling `.filters` — a shared file touches four files. There is no engine vcxproj; engine sources compile into the game projects. DataPacker sources go in `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj`.
- Filter paths mirror the on-disk directory: `Engine/Source/<path>` → `Engine\<path>`, game `Source/<path>` → `Game\<path>`, `Common[/<sub>]` → `Common[\<sub>]`, `DataPacker/Source/<path>` → `DataPacker\<path>`; generated `Output\Data\*.h` → flat `DataFiles`. A new filter needs a `<Filter Include>` entry with a unique GUID (`{8-4-4-4-12}` lowercase hex; any value unique within the file), and every ancestor filter must exist.
- Shader stage sources (`Engine/Data/Shaders/**` `.frag`/`.vert`/`.comp`) are `<None>` items in the client project only — DataPacker compiles them, not MSBuild. Shader `.h` files are ordinary `ClInclude`.

## Workflow

1. For each file, determine affinity: Grep it for a file-wide `#if defined(BT_CLIENT)` / `BT_SERVER` wrap. A guardless engine file appearing only in the client project may be a leaf-documented forced-include exception; verify that documentation and report NOTE rather than FAIL. `Engine.h` aggregation spans alone do not establish affinity.
2. Find the insertion/verification anchor by Grep-ing the vcxproj and .filters for an existing entry of the same element type from the same directory; if none exists (new directory), anchor on the nearest ancestor-directory entry of the same type and extend its relative path. Do not Read whole project files — they are thousands of lines.
3. Add mode: Edit each missing entry next to its anchor (`ClCompile` for `.cpp`, `ClInclude` for `.h` including shader headers, `None` for shader stage sources), matching the sibling entries' form; add the mirrored filter entry, creating missing `<Filter Include>` GUIDs.
4. Verify mode: confirm presence in the correct project(s), absence from the wrong one, and that the filter path mirrors the directory.
5. After integrating newer primary-branch commits into a session branch, rerun Add mode for the session's affected files, then Verify mode for every affected project and filter. Do not hand-merge vcxproj membership or filter mechanics; if both changes assign conflicting affinity or membership semantics, stop for user resolution.

## Report

One line per file — never echo XML:

```
<path> — client|server|both|DataPacker — filter <Filter\Path> — added|verified|NOTE <detail>|FAIL <what's wrong>
```

Append this final footer:

```text
Files changed:
- <project/filter path, or none>
Functions/regions touched:
- <project item/filter group, or none>
Residuals:
- <unfixed FAIL, unresolved NOTE, or none>
```

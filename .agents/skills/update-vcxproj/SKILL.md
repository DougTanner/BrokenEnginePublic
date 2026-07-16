---
name: update-vcxproj
description: >-
  Adds or verifies source-file membership in .vcxproj and .vcxproj.filters
  project files (BrokenEngineSandbox client/server, DataPacker) — client/server
  affinity from BT_CLIENT/BT_SERVER guard scope, filter paths mirroring on-disk
  directories, new-filter GUIDs, shader <None> items. Use when .cpp/.h/shader
  files are added or removed, or when an existing file gains or loses a
  file-wide BT_CLIENT/BT_SERVER guard (affinity change).
  Designed for a cheap subagent — the project XML is large; work via Grep
  anchors, report per-file results, never echo the XML.
allowed-tools: [Read, Write, Edit, Grep, Glob, PowerShell]
---

# Update vcxproj Membership

Add new files to, or verify existing files against, the MSBuild project files. Two modes — infer from the invocation:

For a delegated call, use concise inline reporting. Project-membership work is
not a final-evidence gate.

- **Add** — wire the given files into the correct project(s) and filters.
- **Remove** — remove deleted files from every project and filter file that references them.
- **Verify** — check each given file's membership and filter path; report pass/fail without editing unless asked to fix.

In the C++ change process, invoke this role only when a file was added or
removed, or an existing file gained or lost a file-wide build-affinity guard.
Verify only those affected files, fix each `FAIL` through Add mode, then
reverify it. Ordinary edits to existing files do not trigger project-membership
verification. This skill owns project membership and filter mechanics; callers
do not hand-edit project XML.

## Rules

Read `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md` first — the authoritative rule source (inclusion rules, filter-path mirroring, affinity naming conventions, shader handling). Orientation summary:

- Affinity follows preprocessor-guard scope: shared files (no or partial `BT_CLIENT`/`BT_SERVER` guards) go in **both** game projects; fully wrapped files go **only** in the matching project — and must be removed from the opposite one if the guard scope changed.
- Game projects: `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` (client) and `BrokenEngineSandboxServer.vcxproj`, each with a sibling `.filters` — a shared file touches four files. There is no engine vcxproj; engine sources compile into the game projects. DataPacker sources go in `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj`.
- Filter paths mirror the on-disk directory: `Engine/Source/<path>` → `Engine\<path>`, game `Source/<path>` → `Game\<path>`, `Common[/<sub>]` → `Common[\<sub>]`, `DataPacker/Source/<path>` → `DataPacker\<path>`. Generated headers use `$(GameDataDirectory)\*.h` project items and the flat `DataFiles` filter; verify the property-based form rather than replacing it with `Output\Data`. A new filter needs a `<Filter Include>` entry with a unique GUID (`{8-4-4-4-12}` lowercase hex; any value unique within the file), and every ancestor filter must exist.
- Shader stage sources (`Engine/Data/Shaders/**` `.frag`/`.vert`/`.comp`) are `<None>` items in the client project only — DataPacker compiles them, not MSBuild. Shader `.h` files are ordinary `ClInclude`.

## Workflow

1. For each existing file, determine affinity: Grep it for a file-wide `#if defined(BT_CLIENT)` / `BT_SERVER` wrap. A guardless engine file appearing only in the client project may be a leaf-documented forced-include exception; verify that documentation and report NOTE rather than FAIL. `Engine.h` aggregation spans alone do not establish affinity. For a removed file, confirm it is absent on disk and locate its exact existing project/filter entries instead.
2. Find the insertion/verification anchor by Grep-ing the vcxproj and .filters for an existing entry of the same element type from the same directory; if none exists (new directory), anchor on the nearest ancestor-directory entry of the same type and extend its relative path. Do not Read whole project files; use Grep anchors to keep the large XML out of context.
3. Add mode: Edit each missing entry next to its anchor (`ClCompile` for `.cpp`, `ClInclude` for `.h` including shader headers, `None` for shader stage sources), matching the sibling entries' form; add the mirrored filter entry, creating missing `<Filter Include>` GUIDs. Remove mode: delete the removed file's exact project and filter entries from every affected project; do not remove now-empty `<Filter Include>` groups as incidental cleanup.
4. Verify mode: for existing files, confirm presence in the correct project(s), absence from the wrong one, and that the filter path mirrors the directory; for removed files, confirm no project or filter entry remains.
5. After integrating newer primary-branch commits into a session branch, rerun the applicable Add or Remove mode for the session's affected files, then Verify mode for every affected project and filter. Do not hand-merge vcxproj membership or filter mechanics; if both changes assign conflicting affinity or membership semantics, stop for user resolution.

## Report

Return the complete report inline. Keep any
affinity conflict requiring user resolution visible. One line per file — never echo XML:

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

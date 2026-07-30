---
name: update-vcxproj
description: >-
  Verify or reconcile Visual Studio project and filter membership for added,
  removed, renamed, or whole-file-affinity-changed C++, headers, generated
  headers, shaders, DataPacker files, and AgentTools sources. Use after those
  file or BT_CLIENT/BT_SERVER guard changes to enforce exact client/server/tool
  ownership, XML validity, mirrored filters, and unique filter GUIDs.
allowed-tools: [Read, Write, Edit, Grep, Glob, PowerShell]
---

# Update vcxproj Membership

Main dispatches one `mechanic` with the affected paths, change kind, fixed
baseline, ownership snapshot, and requested mode. The mechanic never delegates
or edits source. Project-membership work is not a final-evidence gate.

Read root `AGENTS.md`, every `AGENTS.md` governing each affected path, and
`Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md`. Scoped
authority may narrow ownership; naming conventions and neighboring entries are
evidence only. Stop on an authority conflict.

## Modes

Accept exactly one explicit mode:

- verify — read-only. Diagnose every affected path and project/filter pair.
- fix — reconcile additions, removals, renames, and whole-file affinity
  changes, then run the complete verify checks. Remove stale old-name and
  forbidden-project entries; do not remove empty filter definitions as cleanup.

Do not infer a mutation request. An ordinary edit without a whole-file affinity
change does not trigger this skill.

## Ownership

Classify special cases before generic extensions:

| Path/type | Intended project ownership | Item/filter |
| --- | --- | --- |
| GLSL stage/include sources (`.vert`, `.frag`, `.comp`, `.geom`, `.tesc`, `.tese`, `.mesh`, `.task`, `.rgen`, `.rmiss`, `.rchit`, `.rahit`, `.rint`, `.rcall`, `.glsl`) under `Engine/Data/Shaders/**` or `Projects/*/Data/Shaders/**` | game client only | `None`; mirror `Engine\Data\Shaders[\<directories>]` or `Game\Data\Shaders[\<directories>]` |
| `$(GameDataDirectory)\*.h` generated headers | game projects selected by scoped authority; omit client-only `Shader.h` from server | `ClInclude`; flat `DataFiles` |
| `.h` under `Engine/Data/Shaders/**` or `Projects/*/Data/Shaders/**` | dual C++/GLSL contract headers use scoped game affinity; GLSL-only include headers use client only | `ClInclude` for C++-consumed headers, otherwise `None`; mirror the owning `Engine\Data\Shaders[...]` or `Game\Data\Shaders[...]` path |
| `Tools/ToolCommon/**` source/header | AgentHarness and WorktreeCli | `ClCompile`/`ClInclude`; `ToolCommon[\<directories>]` |
| `Tools/AgentHarness/**` source/header | AgentHarness | `ClCompile`/`ClInclude`; `AgentHarness[\<directories>]` |
| `Tools/WorktreeCli/**` source/header | WorktreeCli | `ClCompile`/`ClInclude`; `WorktreeCli[\<directories>]` |
| `DataPacker/Source/**` source/header | DataPacker | `ClCompile`/`ClInclude`; `DataPacker[\<directories>]` |
| `Engine/Source/**` source/header | game client, server, or both by structural affinity | `ClCompile`/`ClInclude`; `Engine[\<directories>]` |
| `Projects/BrokenEngineSandbox/Source/**` source/header | game client, server, or both by structural affinity | `ClCompile`/`ClInclude`; `Game[\<directories>]` |
| `Common/**` source/header | game client and server; also DataPacker only when scoped authority assigns it | `ClCompile`/`ClInclude`; `Common[\<directories>]` |

For engine/game files, determine affinity structurally, not from a guard match
or `Engine.h` aggregation span. Skip a UTF-8 BOM, whitespace, and comments;
allow a header's `#pragma once` and a leading include/comment prologue. A
whole-file guard is the next directive, is exactly `#if defined(BT_CLIENT)` or
`#if defined(BT_SERVER)`, and its matching final `#endif` encloses all remaining
substantive declarations or definitions; only whitespace/comments may follow.
Anything else is shared. Preserve a scoped, documented forced-include exception
and report it as `NOTE`.

## Reconcile and Verify

Use targeted searches around same-directory, same-item-type anchors; never load
or echo whole project XML. Match existing relative-path form. For a new filter,
create missing ancestors and a lowercase-hex `{8-4-4-4-12}` GUID unique within
that filters file.

For every affected project and filters file:

1. The mechanic verifies the intended ownership, each affected path's expected
   project membership, and its logical filter text. In fix mode, reconcile only
   the authorized additions, removals, renames, or affinity changes; require
   each intended item exactly once and each forbidden or stale item zero times,
   including old paths for removals/renames.
2. After that check (and again after any fix), invoke the structural validator
   once for each affected project pair:

   ```powershell
   pwsh -NoProfile -File .agents/skills/update-vcxproj/scripts/Test-VcxprojPair.ps1 -ProjectPath <project>.vcxproj
   ```

   Require exit `0`. On exit `1` or `2`, surface its compact JSON `code` and
   violations as `FAIL`, correct only authorized XML, then rerun the command.
   Do not manually recreate the validator's XML parse, full mirror, filter
   declaration, ancestor, or GUID checks after it passes.
3. Leave any failed invariant as a visible `FAIL`; never report a partial
   reconciliation as success.

## Report

Return one line per file and one indented result per relevant project; this
keeps multi-project ownership explicit without XML output:

```text
<path> — <client|server|both|DataPacker|AgentHarness|WorktreeCli|AgentTools>
  <project> — filter <path|none> — verified|fixed|NOTE <detail>|FAIL <detail>
```

Then return:

```text
Files changed: <project/filter paths, or none>
Regions touched: <item groups/filter declarations, or none>
Build required: <exact /compile target — Configuration|x64 for each affected compiled project, or none for verify-only/None-only membership>
Reviewer focus: <authority, affinity, or XML risk, or none>
Residuals: <FAIL/conflict/NOTE requiring action, or none>
```

Use `Debug|x64` for game client/server unless the approved acceptance matrix
names another configuration, `Release|x64` for DataPacker, and the AgentTools
candidate-production route for AgentHarness, WorktreeCli, or ToolCommon source
membership. Never claim a build ran.

---
name: update-vcxproj
description: >-
  Verify or reconcile Visual Studio project/filter membership for file additions,
  removals, renames, or whole-file affinity changes using deterministic validation.
allowed-tools: [Read, Write, Edit, Grep, Glob, PowerShell]
---

# Update vcxproj Membership

One delegated `mechanic` owns only project/filter affinity, membership edits,
and deterministic pair validation. It never delegates, edits source, builds, or
defines landing-commit/finalization policy. Require affected paths, change kind,
session baseline, ownership snapshot, and explicit mode:

- `verify`: read-only diagnosis;
- `fix`: reconcile only authorized additions/removals/renames/whole-file
  affinity changes, then verify.

An ordinary source edit without whole-file affinity change does not trigger this
skill. Read root and scoped `AGENTS.md`, including the VisualStudio2026 project
guidance; stop on conflicting authority.

## Ownership

Classify special cases before generic extension rules:

| Path | Project ownership | Item/filter |
| --- | --- | --- |
| GLSL stages/includes under engine/game `Data/Shaders` | game client | `None`; mirrored `Engine\\Data\\Shaders` or `Game\\Data\\Shaders` |
| generated `$(GameDataDirectory)\\*.h` | scoped game targets; omit client-only `Shader.h` from server | `ClInclude`; `DataFiles` |
| `.h` under shader trees | scoped game affinity if C++ consumed; otherwise client | `ClInclude` or `None`; owning shader filter |
| `Tools/ToolCommon/**` | AgentHarness and WorktreeCli | source item; `ToolCommon[...]` |
| `Tools/AgentHarness/**` | AgentHarness | source item; `AgentHarness[...]` |
| `Tools/WorktreeCli/**` | WorktreeCli | source item; `WorktreeCli[...]` |
| `DataPacker/Source/**` | DataPacker | source item; `DataPacker[...]` |
| `Engine/Source/**` | client/server/both by structural affinity | source item; `Engine[...]` |
| game `Source/**` | client/server/both by structural affinity | source item; `Game[...]` |
| `Common/**` | game client/server; DataPacker only by scoped authority | source item; `Common[...]` |

For engine/game files, a whole-file affinity guard is the first substantive
directive after optional BOM, whitespace, comments, header `#pragma once`, and
include/comment prologue; it is exactly `#if defined(BT_CLIENT)` or
`#if defined(BT_SERVER)`, and its matching final `#endif` encloses all remaining
substantive declarations/definitions. Otherwise the file is shared. Preserve a
documented forced-include exception and report `NOTE`.

## Reconcile and validate

Use targeted searches near same-directory/type anchors; never load or echo whole
XML. Preserve relative path form. For a new filter, add missing ancestors and a
unique lowercase-hex `{8-4-4-4-12}` GUID.

For each affected project pair:

1. Determine intended ownership and exact logical filter. In fix mode, require
   each intended item once and forbidden/stale old-name items zero times.
2. Run once after inspection or repair:

   ```powershell
   pwsh -NoProfile -File .agents/skills/update-vcxproj/scripts/Test-VcxprojPair.ps1 -ProjectPath <project>.vcxproj
   ```

   Require exit `0`. On compact failure, report its `code` and violations, fix
   only authorized XML, and rerun. A pass is authoritative for XML parsing,
   project/filter mirroring, filter declarations/ancestors, and GUID uniqueness;
   do not recreate those checks manually.
3. Leave any unresolved invariant as `FAIL`; partial reconciliation is not
   success.

## Report

```text
<path> — <client|server|both|DataPacker|AgentHarness|WorktreeCli|AgentTools>
  <project> — filter <path|none> — verified|fixed|NOTE <detail>|FAIL <detail>
Files changed: <project/filter paths, or none>
Regions touched: <item groups/filter declarations, or none>
Build required: <exact target/configuration/platform, or none>
Reviewer focus: <authority, affinity, or XML risk, or none>
Residuals: <FAIL/conflict/NOTE requiring action, or none>
```

Use `Debug|x64` for game client/server unless approved otherwise,
`Release|x64` for DataPacker, and the AgentTools promotion route for
tool source membership. Verify-only/`None`-only membership requires no build.
Never claim a build ran.

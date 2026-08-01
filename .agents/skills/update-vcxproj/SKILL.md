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

An ordinary source edit that does not change which executable a whole file
belongs to does not trigger this skill. Read root and scoped `AGENTS.md`,
including the VisualStudio2026 project guidance; stop on conflicting authority.

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

Resolve whole-file affinity and the owning row with one run over every affected
path that exists on disk — additions, new names, affinity changes; never
reconstruct the prologue scan or this table's lookup inline. A removal or an old
rename name needs no record: the authorized change itself says the item goes.

```powershell
pwsh -NoProfile -File .agents/skills/update-vcxproj/scripts/Resolve-VcxprojMembership.ps1 <path> [<path> ...]
```

Each record carries `affinity` (`client`, `server`, `shared`, `unprovable`) with
its `affinityCode`, and `mapping` (`resolved`, `conditional`, `unresolved`,
`non-member`) with its `mappingCode`, `projects`, `itemType`, and `filter`.
`resolved` is authoritative. `conditional` settles everything except the one
named question — generated `$(GameDataDirectory)` headers, `.h` under a shader
tree, DataPacker membership for `Common/**` — which scoped authority decides.
`non-member` is a path deliberately never carried as a project item, a routine
result and not a blocker. Stop and report the path, never guess, when `affinity`
is `unprovable`, when `mapping` is `unresolved`, or when the run omits a record;
on `files.truncated`, reinvoke in smaller batches until every path has a record.
Preserve a documented forced-include exception and report `NOTE`.

## Reconcile and validate

Use targeted searches near same-directory/type anchors; never load or echo whole
XML. Preserve relative path form. For a new filter, add missing ancestors and a
unique lowercase-hex `{8-4-4-4-12}` GUID.

For each affected project pair:

1. Take a `resolved` record's ownership and logical filter as given. On a
   `conditional` record, answer only its one named question from scoped
   authority (`Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md`)
   and take the rest of the record as given; re-derive nothing by hand. In fix
   mode, require each intended item once and forbidden/stale old-name items zero
   times.
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

Return the shared handoff form in `../../references/subagent-reporting.md`,
extended with the per-path membership outcome:

```text
<path> — <client|server|both|DataPacker|AgentHarness|WorktreeCli|AgentTools|non-member>
  <project> — filter <path|none> — verified|fixed|NOTE <detail>|FAIL <detail>
Regions touched: <item groups/filter declarations, or none>
Build required: <exact target/configuration/platform, or none>
Reviewer focus: <authority, affinity, or XML risk, or none>
Residuals: <FAIL/conflict/NOTE requiring action, or none>
```

Use `Debug|x64` for game client/server unless approved otherwise,
`Release|x64` for DataPacker, and the AgentTools promotion route for
tool source membership. Verify-only/`None`-only membership requires no build.
Never claim a build ran.

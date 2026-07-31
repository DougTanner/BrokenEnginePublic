<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-31T20:39:45.369Z","dependsOn":[]} -->
# Session Inventory Region Unroll

## Context

`.agents/scripts/Get-SessionChangeInventory.ps1` reports a wrong region count, and in one case fails outright, whenever a diff produces fewer than two hunks.

`Get-RegionTable` builds `$regions` as a `[Collections.Generic.List[object]]` and ends with a bare `return $regions` (line 389). PowerShell unrolls a returned collection into the pipeline, so the caller at line 592 — `$fullRegions = Get-RegionTable ...` — does not receive the list when it holds zero or one element:

- One region: `$fullRegions` becomes that single `[ordered]` region dictionary, and `$fullRegions.Count` returns its key count (9) instead of `1`. Reproduced this session in a scratch repository (one-line edit to one tracked file, `-Regions`): stdout reported `"regions":{"full":9,"emitted":1}` and `"truncated":true` — a spurious truncation signal on a one-region session, while `"regions"` itself correctly carried exactly one region. A two-region run over the same fixture reported the correct `{"full":2,"emitted":2}` and `"truncated":false`, confirming the fault appears only below two elements.
- Zero regions: `$fullRegions` becomes `$null`, and under `Set-StrictMode -Version Latest` the same `$fullRegions.Count` at line 604 throws. The same scratch run with no working-tree change returned `"status":"error"`, `"code":"internal.error"`, `"message":"The property 'Count' cannot be found on this object. Verify that the property exists."`, with `"regions":null`. This reaches any `-Regions` run whose diff has no text hunks — for example a change confined to binary files or file modes.

The consequence is a false truncation signal, and a hard failure, in exactly the small sessions the inventory is most often used on. Consumers that gate on `truncated` or on `truncation.regions.full` therefore see a session as over cap when it is not.

The repository already has the fix pattern: `.agents/scripts/Find-SessionDebugResidue.ps1` `Get-NewSideLine` returns `, $script:NewSideLines[$Path]` (lines 104 and 114) precisely to defeat this unrolling.

The script's owning plan (`Documents/Plans/Agents/SessionChangeInventoryScript.md`) is completed and deleted, and `Documents/Plans/Agents/ReviewSkillInventoryAdoption.md` `## Out of scope` routes any inventory-script defect to a follow-up, so this Plan owns the fix. No live Plan covers this root cause; `plan validate` reports `status: valid`, `code: ok` over the current tree.

## Design

Wrap the returned list so the caller always receives a collection: change the final `return $regions` of `Get-RegionTable` to the leading-comma array form already used in `Find-SessionDebugResidue.ps1`, so a zero- and a one-element table arrive as a list whose `Count` is `0` and `1`.

Audit the other `return` statements in the same script for the same hazard and fix only those that are actually exposed, so the change stays at the defect:

- `Get-InventoryRawRow` (`return $rows`) and `Get-InventoryUntrackedEntry` (`return $entries`) return the same list type. Their call sites at lines 556 and 562 consume the result through `foreach` and `@(...)`, which survive unrolling; confirm that before deciding, and leave a call site that is already safe unchanged rather than hardening it speculatively.
- The functions returning `[ordered]` dictionaries or hashtables (`Get-RoutingTrigger`, `Get-InventoryCount`, `Get-InventoryBinaryPath`, `Get-InventoryIdentity`, `Get-LandingState`) are not unrolled and are not part of this change.

No output field, cap, schema version, or truncation policy changes; only the counts stop being wrong.

## Critical files

- `.agents/scripts/Get-SessionChangeInventory.ps1` — `Get-RegionTable` final `return` (line 389); the `$fullRegions` call site and the `truncation.regions` computation (lines 591-604, 618-623) are read to confirm the fix, not rewritten.
- `.agents/scripts/Find-SessionDebugResidue.ps1` — read-only reference for the leading-comma pattern (`Get-NewSideLine`, lines 104 and 114).

## In scope

- `.agents/scripts/Get-SessionChangeInventory.ps1` `Get-RegionTable`: the final `return` statement, changed to the leading-comma array-wrapped form.
- `.agents/scripts/Get-SessionChangeInventory.ps1`: the same single-element-unroll fix applied to `Get-InventoryRawRow` and/or `Get-InventoryUntrackedEntry` only if the audit above proves their call sites are actually exposed.

## Out of scope

- Any change to the `broken-engine-session-change-inventory/v1` schema, field names, status codes, or messages.
- Any change to the caps (`MaximumEntries`, `MaximumRegions`, `MaximumOutputBytes`), the shed order, or the output-byte fixed-point loop.
- Region parsing itself: hunk-header matching, path resolution, `kind` classification, symbol truncation, and untracked-file diffing.
- `-Landing` and `-EmitTargetManifest` mode behavior, the manifest schema, and the manifest corpus rules.
- Any `.agents/skills/**` file, including consumers of this script; no skill prose changes.
- Adding validation, defensive checks, tests, or diagnostics beyond the return fix.

## Risk tier and invariants

Tier 2 — scoped tool behavior in one agent script, with no engine runtime, determinism/CRC, wire, serialization, save/replay, threading, or build/bootstrap coordination surface.

Invariants: reported `truncation.regions.full` equals the number of regions the diff actually produced; `truncated` is true only when something was really shed; a `-Regions` run over any diff, including one with no text hunks, completes with `status: pass`; the emitted `regions` array and every other output field are byte-identical to today's output for diffs of two or more regions.

## Acceptance criteria

- A `-Regions` run over a diff producing exactly one hunk reports `truncation.regions` as `{"full":1,"emitted":1}`, `truncated:false`, `status:"pass"`, and exit `0`, with the single region still present in `regions`.
- A `-Regions` run over a change with no text hunks (for example a binary-only or mode-only diff) reports `truncation.regions` as `{"full":0,"emitted":0}`, `truncated:false`, `status:"pass"`, and exit `0` — no `internal.error` and no `Count` property failure.
- A `-Regions` run over a diff producing two or more hunks reports the same `regions` array and the same `truncation.regions` values as the pre-change script over the same fixture.
- A default-mode run and a `-Landing` run over the same fixture produce output identical to the pre-change script.

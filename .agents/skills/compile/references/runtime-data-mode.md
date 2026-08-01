# Select runtime data mode

Every worktree game build uses one data mode and one authoritative directory for both client and server:

- Shared is the default for ordinary code changes. Set `$GameDataDirectory` to `$PRIMARY\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output\Data`; this mode consumes primary generated headers/packs and disables every DataPacker build/export step.
- Local is mandatory when tracked changes from `$BASELINE`, staged changes, unstaged changes, or untracked files touch `DataPacker/**`, `Engine/Data/**`, `Projects/BrokenEngineSandbox/Data/**`, `Common/DataFile.h`, generated-header logic, exporter versions/fingerprints, compression, chunk layout, or pack/manifest contracts. Set `$GameDataDirectory` to `$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output\Data`.
- A user may force Local. Never force Shared over a Local trigger. Local never falls back to Shared data. Generate Local output only with explicit authorization; a user-approved plan or acceptance criterion requiring shader or data repack is authorization.
- An absent worktree `Data` output never blocks Local. A linked worktree that has never generated has no `Output\Data` directory at all; that is the legitimate expected starting state, not missing setup. On an authorized Local generation build DataPacker seeds it from the primary checkout itself — the Local generation section below states the exact mechanism and its conditions. Never stop, ask the user to pre-stage output, or hand-run an export because the directory is absent. The only thing an agent must supply is generation *authorization*; the genuine blockers are validation and environment failures DataPacker reports itself — an unrecognized reparse point, absent primary output, or insufficient disk — never a user-prepared directory.
- Deletion-only exception: pure deletions of source asset files under `Engine/Data/**` or `Projects/BrokenEngineSandbox/Data/**` do not trigger Local when a repository-wide search proves nothing tracked references the deleted asset's generated identity (its generated CRC constant, chunk, or path). With `RunDataPacker=false` the build consumes only pre-existing generated output, which a source deletion cannot alter — the dead chunk persists in the published pack and the unused constant in the generated header until the next real DataPacker export drops both. Record the reference-search evidence with the mode selection. Any addition, modification, rename, exporter, or contract change keeps the Local requirement.

The game projects detect the authoritative repository-root Git marker. A linked worktree has a `.git` file and defaults `RunDataPacker=false`; the primary checkout has a `.git` directory and preserves ordinary Local Visual Studio behavior by defaulting true. An explicit Local `RunDataPacker=true` permits deliberate worktree generation; Shared rejects true.

The resolved context's `changedPaths` is the changed-path set, and its `dataBuildMode` covers the four path triggers only (`dataBuildModeDerivation: path-rules-only`). The remaining triggers above stay your judgment: if a changed file may affect generated or serialized bytes and the path rules do not prove otherwise, select Local even when the script reports Shared.

Take `$DataBuildMode`, `$GameDataDirectory`, and `$GeneratedDataIncludeRoot` from the resolved context (consumers include `Data/...`); when your own judgment selects Local over a reported Shared, replace all three together — set `$DataBuildMode` to `Local` and use the Local directory above with its normalized parent — so mode and directories never disagree. Set `$RunDataPacker` to `'false'` for Shared and ordinary Local builds, or to `'true'` only for the first game build in an authorized Local generation. Pass these exact properties to every client/server `.sln` or `.vcxproj` build, including `--files`:

```powershell
$DataProperties = @(
	"/p:DataBuildMode=$DataBuildMode",
	"/p:RunDataPacker=$RunDataPacker",
	"/p:GameDataDirectory=$GameDataDirectory",
	"/p:GeneratedDataIncludeRoot=$GeneratedDataIncludeRoot"
)
```

When `$RunDataPacker -eq 'true'`, the game project's nested DataPacker build also requires `DevEnvDir`. Re-run the context script with `-IncludeDevEnvDir` and take its `devEnvDir` value, which already carries the trailing separator; `dev-env-dir.unresolved` blocks the generation build. Append the property only for that generation build:

```powershell
$GenerationDataProperties = @($DataProperties) + "/p:DevEnvDir=$($Context.devEnvDir)"
```

Use `$GenerationDataProperties` only for the first `RunDataPacker=true` build. For every later build, set `$RunDataPacker = 'false'`, reconstruct `$DataProperties` from the four-property block above, and pass that array; `DevEnvDir` must not leak into subsequent calls.

Every selected Data directory must be an absolute ordinary non-reparse directory containing exactly the 26 generated files: `Data.h`, `DataTypes.h`, and one nonempty header, manifest, and pack for each of `Audio`, `Font`, `Scene`, `Islands`, `Model`, `Shader`, `Texture`, and `Raw`. Before the first game build, create an ignored receipt parent under `$ROOT\Temp`, invoke `scripts/New-DataOracleReceipt.ps1` with the exact normalized Data path, `Shared` or `Local` mode, fixed 40-hex baseline, and absolute receipt path, and require exit `0` plus `broken-engine-data-oracle-producer-result/v1` `status:pass`, `code:ok`. Retain its receipt path and SHA-256. Before and after every consuming build, invoke `scripts/Test-DataOracleReceipt.ps1` with that exact receipt path/hash, Data path, mode, and baseline; require exit `0` plus `broken-engine-data-oracle-verifier-result/v1` `status:pass`, `code:ok`. Missing, extra, empty, replaced, reparse, or changed entries fail closed.

Ordinary Shared work otherwise remains unchanged: use primary Data, keep `RunDataPacker=false`, and perform no materialization or export. A Local receipt and a Shared receipt are independent identities; never require their aggregate or entry digests to equal. For Local work, also issue and verify a separate Shared receipt for primary Data before and after each game build so a change to primary data fails closed.

Local mode may build the worktree Release DataPacker as a standalone compile check. Before an authorized same-baseline Local generation, invoke the worktree DataPacker exactly once as `& "$ROOT\DataPacker\Platforms\VisualStudio2026\Output\DataPacker.exe" --materialize-data "$ROOT\Engine\Data" "$ROOT\Projects\BrokenEngineSandbox\Data" $GameDataDirectory`; require exit `0`, then require Local Data to be an ordinary non-reparse directory and issue its pre-generation oracle receipt. This mode performs no exports and never creates, inspects, or materializes Attribution. Set `$RunDataPacker = 'true'` only on the first game build so the normal producer runs. In a validated linked worktree, dirty output uses copy-on-write materialization, staging primary files and atomically replacing only the affected recognized link.

Agent-driven Local generation forbids Gaea by default. Scope `BT_DATAPACKER_FORBID_GAEA_EXPORT=1` around the entire first `RunDataPacker=true` WorktreeCli call and restore the caller's prior environment exactly in `finally`. Only an explicit user-approved plan or acceptance criterion that requires regenerating Gaea output authorizes omitting/clearing this guard; changed island inputs, DataPacker code, fingerprints, or cache state alone never authorize a Gaea run. If the guard blocks a dirty Gaea route, the generation build failed: report the exception and do not clear the guard or retry without that explicit approval. `BT_DATAPACKER_FORBID_EXPENSIVE_EXPORT` retains its broader independent meaning; never clear either caller-provided guard.

```powershell
$HadGaeaGuard = Test-Path Env:BT_DATAPACKER_FORBID_GAEA_EXPORT
$PreviousGaeaGuard = $env:BT_DATAPACKER_FORBID_GAEA_EXPORT
try
{
	$env:BT_DATAPACKER_FORBID_GAEA_EXPORT = '1'
	# Invoke the first foreground WorktreeCli game build with RunDataPacker=true and
	# $GenerationDataProperties here; require success.
}
finally
{
	if ($HadGaeaGuard) { $env:BT_DATAPACKER_FORBID_GAEA_EXPORT = $PreviousGaeaGuard }
	else { Remove-Item Env:BT_DATAPACKER_FORBID_GAEA_EXPORT -ErrorAction SilentlyContinue }
}
```

After that build, issue a new Local oracle receipt and verify it plus the independent primary Shared receipt. Set `$RunDataPacker = 'false'` for the other target and every subsequent build. Runtime-only reuse always keeps `RunDataPacker=false` and must preserve the supplied Local receipt exactly. Producer or data changes may replace the pre-generation Local receipt only when the approved plan or acceptance criterion authorizes the resulting content delta; otherwise any digest change is a blocker. The Gaea guards above remain in force for the producer call.

Without generation authorization, require a current Local oracle receipt prepared after the relevant worktree changes and verify it exactly. If absent or stale, stop with `Local data is missing or stale; Local generation authorization required` and do not build, export, materialize, or fall back.

Verify the independent primary Shared receipt after each Local game build; any primary write fails the workflow. Verify the final selected receipt after the last game build and hand both receipt identities to the harness. Never copy worktree DataPacker/data source changes into `$PRIMARY` to test them.

<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-01T19:43:59.892Z","dependsOn":[]} -->
# Fix: Resolve Shared Script Modules When A Skill Script Runs Through The `.claude` Mirror

## Context

`.claude/skills` is a symlink to `.agents/skills` (`Documents/FreshMachineSetup.md:7`). PowerShell sets `$PSScriptRoot` from the path used to launch the script and does not canonicalize the symlink, so a script launched as `.claude\skills\<skill>\scripts\X.ps1` sees `$PSScriptRoot` under `.claude`. Every `Join-Path $PSScriptRoot '..\..\..\scripts\...'` reference then points at `.claude\scripts`, which does not exist; only `.agents\scripts` does.

Reproduced at session baseline `274c54576fe6521a533baf2a6c6e6ca9b82381fa` with `.agents/skills/compile/scripts/Test-DataOracleReceipt.ps1:13`:

```
pwsh -NoProfile -File .claude/skills/compile/scripts/Test-DataOracleReceipt.ps1 -ReceiptPath x -ReceiptSha256 y -ExpectedDataRoot z -ExpectedMode Local -ExpectedBaseline b

Import-Module: ...\.claude\skills\compile\scripts\Test-DataOracleReceipt.ps1:13
The specified module '...\.claude\skills\compile\scripts\..\..\..\scripts\AgentScriptCommon.psm1' was not loaded because no valid module file was found in any module directory.
```

The same invocation through `.agents/skills/compile/scripts/Test-DataOracleReceipt.ps1` reaches the script body and returns its normal `broken-engine-data-oracle-verifier-result/v1` envelope, proving the only difference is the launch path.

This is not specific to one script. 17 tracked skill scripts contain a `..\..\..\scripts` reference. Five already carry the repository's established mirror fallback — test the authoritative path, and when it is absent re-point at `..\..\..\..\.agents\scripts` — for example `.agents/skills/next-plan/scripts/NextPlanWorkflowCommon.psm1`:

```powershell
$sharedScripts = Join-Path $PSScriptRoot '..\..\..\scripts'
if (-not (Test-Path -LiteralPath (Join-Path $sharedScripts 'FinalizeWorkflowCommon.psm1'))) {
	$sharedScripts = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts'
}
```

The remaining 12 scripts have no such guard and fail under the mirror path. References that walk four levels to the repository root (`..\..\..\..`) are unaffected, because both `.agents\skills\<skill>\scripts` and `.claude\skills\<skill>\scripts` are four levels below the repository root.

## Design

Apply the existing guard pattern above — unchanged in shape, resolving the shared directory once per script and reusing it — to every `..\..\..\scripts` reference in the 12 unguarded scripts. Both `Import-Module` targets and sibling-script/directory paths (`Get-SessionChangeInventory.ps1`, `Provision-WorktreeThirdParty.ps1`, `Detect-Python.ps1`, `Test-AgentToolsCapabilities.ps1`, and the module-source directory used by the fixture scripts) resolve through the same fallback.

Under an `.agents` launch the authoritative path exists, the fallback never fires, and behavior is byte-identical to today. The fallback only changes the previously failing `.claude` launch.

Rejected alternative: adding a tracked `.claude/scripts` symlink. It would need a second Developer-Mode-dependent symlink with the same broken-checkout failure mode `Documents/FreshMachineSetup.md:7-9` already documents, and the repository already has a working in-tree pattern.

## Critical files

- `.agents/skills/agent-harness/scripts/Invoke-HarnessClaim.ps1`
- `.agents/skills/codex-review/scripts/New-CodexReviewPrompt.ps1`
- `.agents/skills/compile/scripts/New-DataOracleReceipt.ps1`
- `.agents/skills/compile/scripts/Resolve-CompileContext.ps1`
- `.agents/skills/compile/scripts/Test-DataOracleReceipt.ps1`
- `.agents/skills/finalize-changes/scripts/Invoke-AgentToolsPromotion.ps1`
- `.agents/skills/finalize-changes/scripts/Test-AgentToolsPromotionFixtures.ps1`
- `.agents/skills/finalize-changes/scripts/Test-FinalizeWorkflowFixtures.ps1`
- `.agents/skills/finalize-changes/scripts/Test-LandingLockStatusFixtures.ps1`
- `.agents/skills/finalize-changes/scripts/Wait-AgentToolsQuiescence.ps1`
- `.agents/skills/gaea2-shared/scripts/Invoke-Gaea2Python.ps1`
- `.agents/skills/update-vcxproj/scripts/Resolve-VcxprojMembership.ps1`

Read-only pattern evidence: `.agents/skills/next-plan/scripts/NextPlanWorkflowCommon.psm1`, `.agents/skills/finalize-changes/scripts/Invoke-FinalizeLanding.ps1`, `Invoke-FinalizeLockClaim.ps1`, `Invoke-FinalizeApprovalPreparation.ps1`, `Show-FinalizeApprovalReview.ps1`.

## In scope

- The 12 scripts listed under Critical files: replace each `Join-Path $PSScriptRoot '..\..\..\scripts...'` expression with the established resolve-once-plus-`Test-Path`-fallback form, preserving each script's existing import order, `-Force`/`-DisableNameChecking` switches, and result contracts.

## Out of scope

- The five scripts that already carry the fallback; they are read-only pattern evidence.
- Any `..\..\..\..` repository-root reference, any `..\references\` intra-skill reference, and any `.agents/scripts/*` script, all of which resolve correctly.
- Adding a `.claude/scripts` symlink, changing the `.claude/skills` symlink, or changing checkout/bootstrap policy in `Documents/FreshMachineSetup.md`.
- Any `SKILL.md` body, frontmatter, `agents/openai.yaml`, documentation cross-link, or `.codex` mirror.
- Any change to script logic, parameters, output schemas, exit codes, or the shared modules under `.agents/scripts/`.
- Any C++, shader, or `Projects/` change.

## Risk tier and invariants

**Tier 3** — three of the affected scripts (`Invoke-AgentToolsPromotion.ps1`, `Wait-AgentToolsQuiescence.ps1`, `Resolve-CompileContext.ps1`) are shared AgentTools build and bootstrap coordination that can block other sessions.

- An `.agents`-path launch must load exactly the same modules and scripts as before; the fallback branch may only be reachable when the authoritative path is absent.
- Landing-lock, AgentTools promotion, and quiescence semantics are untouched; only path resolution changes.
- Every script keeps its existing JSON result schema, exit codes, and parameter contract.

## Acceptance criteria

- A repository search finds no `..\..\..\scripts` reference in `.agents/skills/**` that lacks the fallback guard.
- `pwsh -NoProfile -File .claude/skills/compile/scripts/Test-DataOracleReceipt.ps1 -ReceiptPath x -ReceiptSha256 y -ExpectedDataRoot z -ExpectedMode Local -ExpectedBaseline b` no longer emits the `AgentScriptCommon.psm1` `Import-Module` failure and instead returns the same `broken-engine-data-oracle-verifier-result/v1` envelope the `.agents` launch returns.
- The repository's own fixture scripts still pass from their `.agents` paths: `Test-FinalizeWorkflowFixtures.ps1`, `Test-AgentToolsPromotionFixtures.ps1`, `Test-LandingLockStatusFixtures.ps1`, `Test-DataOracleReceiptFixtures.ps1`, and `Test-CodexReviewPromptFixtures.ps1`.
- `WorktreeCli.exe plan validate` exits `0` with `status: valid`.

[CmdletBinding()]
param([Parameter(Mandatory)][string] $WorktreeCliExecutable)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$utf8 = [Text.UTF8Encoding]::new($false, $true)

function Assert-True([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Set-Plan([string] $Root, [string] $Path, [string] $CreatedUtc, [string[]] $DependsOn = @()) {
	$diskPath = Join-Path $Root $Path
	[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($diskPath)) | Out-Null
	$metadata = [ordered]@{ createdUtc = $CreatedUtc; dependsOn = @($DependsOn) } | ConvertTo-Json -Depth 3 -Compress
	[IO.File]::WriteAllText($diskPath, "<!-- broken-engine-plan/v1 $metadata -->`n# $([IO.Path]::GetFileNameWithoutExtension($Path))`n", $utf8)
}
function Invoke-Cli([int] $ExpectedExit, [string[]] $Arguments) {
	$output = @(& $WorktreeCliExecutable @Arguments 2>&1)
	$exit = $LASTEXITCODE
	Assert-True ($exit -eq $ExpectedExit) "WorktreeCli exited ${exit}, expected ${ExpectedExit}: $($Arguments -join ' '); output=$($output -join "`n")"
	try { $json = ($output -join "`n" | ConvertFrom-Json -Depth 100 -ErrorAction Stop) } catch { throw "WorktreeCli did not return one JSON value: $($output -join "`n")" }
	foreach ($field in @('schemaVersion','operation','status','code','message')) { Assert-True ($json.PSObject.Properties.Name -ccontains $field) "Scheduler response omits '$field'." }
	Assert-True ($json.schemaVersion -eq 1) 'Scheduler response schemaVersion is not 1.'
	return $json
}
function Assert-Result($Result, [string] $Operation, [string] $Status, [string] $Code) {
	Assert-True ($Result.operation -ceq $Operation -and $Result.status -ceq $Status -and $Result.code -ceq $Code) "Unexpected result: $($Result | ConvertTo-Json -Depth 20 -Compress)"
}
function Track-Plan([string] $Worktree, [string] $Path) { & git.exe -C $Worktree add -- $Path | Out-Null }
function Remove-TemporaryPlan([string] $Worktree, [string] $Path) {
	& git.exe -C $Worktree reset -- $Path | Out-Null
	$diskPath = Join-Path $Worktree $Path
	if (Test-Path -LiteralPath $diskPath) { Remove-Item -LiteralPath $diskPath -Force }
}
function Commit([string] $Worktree, [string] $Message) { & git.exe -C $Worktree add --all -- Documents | Out-Null; & git.exe -C $Worktree commit -m $Message | Out-Null }

$root = Join-Path ([IO.Path]::GetTempPath()) ('broken-engine-plan-scheduler-' + [Guid]::NewGuid().ToString('N'))
$originalLocalAppData = [Environment]::GetEnvironmentVariable('LOCALAPPDATA')
try {
	$primary = Join-Path $root 'primary'; $session = Join-Path $root 'session'
	$localAppData = Join-Path $root 'localappdata'
	New-Item -ItemType Directory -Force -Path $primary, $localAppData | Out-Null
	$env:LOCALAPPDATA = $localAppData
	& git.exe -C $primary init --initial-branch main | Out-Null
	$repo = (& git.exe -C $primary rev-parse --path-format=absolute --git-common-dir).Trim()
	& git.exe -C $primary config user.email fixture@example.invalid; & git.exe -C $primary config user.name fixture
	& git.exe -C $primary config core.autocrlf false
	& git.exe -C $primary config core.longpaths true

	# Valid metadata is byte-zero, exact-key, UTF-8 without BOM, and immutable relative to baseline.
	Set-Plan $primary 'Documents/Plans/Test/Older.md' '2024-01-01T00:00:00.000Z'
	Set-Plan $primary 'Documents/Plans/Test/Alpha.md' '2024-01-02T00:00:00.000Z'
	Set-Plan $primary 'Documents/Plans/Test/Zulu.md' '2024-01-02T00:00:00.000Z'
	[IO.File]::WriteAllText((Join-Path $primary 'Documents/Plans/Test/Reference.md'), '# Manual reference', $utf8)
	Commit $primary 'baseline'
	$baseline = (& git.exe -C $primary rev-parse HEAD).Trim()
	& git.exe -C $primary worktree add -b fixture-session $session $baseline | Out-Null
	New-Item -ItemType Directory -Force -Path (Join-Path $session 'Temp') | Out-Null
	$valid = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $valid 'validate' 'valid' 'ok'; Assert-True ($valid.plans.Count -eq 3) 'Manual/reference Markdown became executable.'
	Assert-True ($valid.plans[0].path -ceq 'Documents/Plans/Test/Older.md') 'Validation did not retain deterministic inventory order.'

	# Metadata boundary: BOM means the marker is not byte zero; malformed markers and unknown keys are invalid rather than
	# silently manual. A marker cannot be removed after baseline, while a newly created executable Plan is valid.
	$bomPlan = 'Documents/Plans/Test/Bom.md'; $bomPath = Join-Path $primary $bomPlan; [IO.File]::WriteAllText($bomPath, '<!-- broken-engine-plan/v1 {"createdUtc":"2024-01-03T00:00:00.000Z","dependsOn":[]} -->', [Text.UTF8Encoding]::new($true)); Track-Plan $primary $bomPlan
	$bom = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $bom 'validate' 'valid' 'ok'; Assert-True (@($bom.plans | Where-Object { $_.path -ceq 'Documents/Plans/Test/Bom.md' }).Count -eq 0) 'BOM-prefixed marker became executable metadata.'
	Remove-TemporaryPlan $primary $bomPlan
	$strictPlan = 'Documents/Plans/Test/Strict.md'; $strictPath = Join-Path $primary $strictPlan; [IO.File]::WriteAllText($strictPath, '<!-- broken-engine-plan/v1 {"createdUtc":"2024-01-03T00:00:00.000Z","dependsOn":[],"extra":true} -->', $utf8); Track-Plan $primary $strictPlan
	$strict = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $strict 'validate' 'invalid' 'invalid-plans'; Assert-True (@($strict.diagnostics | Where-Object { $_.plan -ceq 'Documents/Plans/Test/Strict.md' }).Count -eq 1) 'Unknown metadata key was accepted.'
	Remove-TemporaryPlan $primary $strictPlan
	$noncanonicalPlan = 'Documents/Plans/Test/NoncanonicalTimestamp.md'; Set-Plan $primary $noncanonicalPlan '2024-1-002T00:00:00.000Z'; Track-Plan $primary $noncanonicalPlan
	$invalidCalendarPlan = 'Documents/Plans/Test/InvalidCalendar.md'; Set-Plan $primary $invalidCalendarPlan '2024-02-30T00:00:00.000Z'; Track-Plan $primary $invalidCalendarPlan
	$timestampValidation = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $timestampValidation 'validate' 'invalid' 'invalid-plans'; Assert-True (@($timestampValidation.diagnostics | Where-Object { $_.plan -cin @($noncanonicalPlan,$invalidCalendarPlan) }).Count -eq 2) 'Noncanonical or calendar-invalid createdUtc was accepted.'
	Remove-TemporaryPlan $primary $noncanonicalPlan; Remove-TemporaryPlan $primary $invalidCalendarPlan
	$deepPlan = 'Documents/Plans/Test/Deep/segment-00000001/segment-00000002/segment-00000003/segment-00000004/segment-00000005/segment-00000006/segment-00000007/segment-00000008/segment-00000009/segment-00000010/Deep.md'
	Set-Plan $primary $deepPlan '2024-01-03T00:00:00.000Z'; Track-Plan $primary $deepPlan
	$deep = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $deep 'validate' 'valid' 'ok'; Assert-True (@($deep.plans | Where-Object { $_.path -ceq $deepPlan }).Count -eq 1) 'Deep tracked Plan path was not enumerated.'
	Remove-TemporaryPlan $primary $deepPlan

	# Baseline deletions are classified by baseline metadata: manual/reference files may disappear, while executable Plans
	# require a terminal receipt.
	$referencePath = Join-Path $primary 'Documents/Plans/Test/Reference.md'; Remove-Item -LiteralPath $referencePath -Force
	$manualStalePlan = 'Documents/Plans/Test/ManualStale.md'; Set-Plan $primary $manualStalePlan '2024-01-03T00:00:00.000Z' @('Documents/Plans/Test/Reference.md'); Track-Plan $primary $manualStalePlan
	$manualDeletion = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $manualDeletion 'validate' 'valid' 'ok'; Assert-True (@($manualDeletion.plans | Where-Object { $_.path -ceq $manualStalePlan }).Count -eq 1 -and @($manualDeletion.notices | Where-Object { $_.plan -ceq $manualStalePlan -and $_.code -ceq 'stale-dependency' }).Count -eq 1) 'Deleted baseline manual dependency did not become a satisfied stale-edge notice.'
	Remove-TemporaryPlan $primary $manualStalePlan
	& git.exe -C $primary checkout -- Documents/Plans/Test/Reference.md | Out-Null
	$olderPath = Join-Path $primary 'Documents/Plans/Test/Older.md'; Remove-Item -LiteralPath $olderPath -Force
	$executableDeletion = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $executableDeletion 'validate' 'invalid' 'invalid-plans'; Assert-True (@($executableDeletion.diagnostics | Where-Object code -ceq 'baseline-plan-missing-or-demoted').Count -eq 1) 'Executable baseline deletion without terminal receipt was accepted.'
	& git.exe -C $primary checkout -- Documents/Plans/Test/Older.md | Out-Null
	$olderPath = Join-Path $primary 'Documents/Plans/Test/Older.md'; [IO.File]::WriteAllText($olderPath, '# Demoted', $utf8)
	$demoted = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $demoted 'validate' 'invalid' 'invalid-plans'; Assert-True (@($demoted.diagnostics | Where-Object code -ceq 'baseline-plan-missing-or-demoted').Count -eq 1) 'Executable marker demotion was not rejected.'
	& git.exe -C $primary checkout -- Documents/Plans/Test/Older.md | Out-Null
	Remove-Item -LiteralPath (Join-Path $primary 'Documents/Plans/Test/Alpha.md') -Force
	[IO.File]::WriteAllText((Join-Path $primary 'Documents/Plans/Test/Zulu.md'), '# Demoted', $utf8)
	Commit $primary 'terminal Plans removed by another session'
	& git.exe -C $session merge --ff-only main | Out-Null
	Set-Plan $session 'Documents/Plans/Test/Alpha.md' '2024-01-02T00:00:00.000Z'
	Set-Plan $session 'Documents/Plans/Test/Zulu.md' '2024-01-02T00:00:00.000Z'
	Track-Plan $session 'Documents/Plans/Test/Alpha.md'
	Track-Plan $session 'Documents/Plans/Test/Zulu.md'
	Remove-Item -LiteralPath (Join-Path $session 'Documents/Plans/Test/Alpha.md') -Force
	[IO.File]::WriteAllText((Join-Path $session 'Documents/Plans/Test/Zulu.md'), '# Demoted', $utf8)
	$localChanges = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$session,'--baseline',$baseline)
	Assert-Result $localChanges 'validate' 'invalid' 'invalid-plans'
	Assert-True (@($localChanges.diagnostics | Where-Object { $_.plan -ceq 'Documents/Plans/Test/Alpha.md' -and $_.code -ceq 'baseline-plan-missing-or-demoted' }).Count -eq 1) 'Indexed Plan missing only from the local working tree inherited the primary-advance notice.'
	Assert-True (@($localChanges.diagnostics | Where-Object { $_.plan -ceq 'Documents/Plans/Test/Zulu.md' -and $_.code -ceq 'baseline-plan-missing-or-demoted' }).Count -eq 1) 'Staged reintroduction canceled by primary-matching working bytes inherited the primary-advance notice.'
	& git.exe -C $session reset --hard main | Out-Null
	$primaryAdvance = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$session,'--baseline',$baseline)
	Assert-Result $primaryAdvance 'validate' 'valid' 'ok'; Assert-True (@($primaryAdvance.notices | Where-Object code -ceq 'missing-plan-file').Count -eq 2) 'Reconciled primary Plan removal or demotion remained blocking.'
	Set-Plan $primary 'Documents/Plans/Test/Alpha.md' '2024-01-02T00:00:00.000Z'
	Set-Plan $primary 'Documents/Plans/Test/Zulu.md' '2024-01-02T00:00:00.000Z'
	Commit $primary 'restore primary advance fixtures'
	& git.exe -C $session merge --ff-only main | Out-Null
	$newPlanPath = 'Documents/Plans/Test/New.md'; Set-Plan $primary $newPlanPath '2024-01-03T00:00:00.000Z'; Track-Plan $primary $newPlanPath
	$newPlan = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $newPlan 'validate' 'valid' 'ok'; Assert-True (@($newPlan.plans | Where-Object { $_.path -ceq $newPlanPath }).Count -eq 1) 'New tracked executable Plan was omitted.'; Remove-TemporaryPlan $primary $newPlanPath

	# Targeted validation must only report the selected canonical executable Plan and reject immutable createdUtc mutation.
	$targeted = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline,'--plan','Documents/Plans/Test/Older.md')
	Assert-Result $targeted 'validate' 'valid' 'ok'; Assert-True ($targeted.plans.Count -eq 1 -and $targeted.plans[0].path -ceq 'Documents/Plans/Test/Older.md') 'Targeted validation did not select only requested Plan.'
	$olderPath = Join-Path $primary 'Documents/Plans/Test/Older.md'; [IO.File]::WriteAllText($olderPath, "<!-- broken-engine-plan/v1 {`"createdUtc`":`"2024-01-03T00:00:00.000Z`",`"dependsOn`":[]} -->`n# Older", $utf8)
	$immutable = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $immutable 'validate' 'invalid' 'invalid-plans'; Assert-True (@($immutable.diagnostics | Where-Object code -ceq 'immutable-created-utc').Count -eq 1) 'createdUtc mutation was not quarantined.'
	& git.exe -C $primary checkout -- Documents/Plans/Test/Older.md | Out-Null

	# Dependencies: existing executable nodes block, missing nodes are satisfied notices, invalid/manual nodes block, and a
	# self/cycle only quarantines its component while unrelated plan remains claimable.
	Set-Plan $primary 'Documents/Plans/Test/Dependent.md' '2024-01-04T00:00:00.000Z' @('Documents/Plans/Test/Older.md')
	Set-Plan $primary 'Documents/Plans/Test/Stale.md' '2024-01-05T00:00:00.000Z' @('Documents/Plans/Test/Deleted.md')
	Set-Plan $primary 'Documents/Plans/Test/Self.md' '2024-01-06T00:00:00.000Z' @('Documents/Plans/Test/Self.md')
	Set-Plan $primary 'Documents/Plans/Test/CycleA.md' '2024-01-07T00:00:00.000Z' @('Documents/Plans/Test/CycleB.md')
	Set-Plan $primary 'Documents/Plans/Test/CycleB.md' '2024-01-08T00:00:00.000Z' @('Documents/Plans/Test/CycleA.md')
	Set-Plan $primary 'Documents/Plans/Test/ManualBlocked.md' '2024-01-09T00:00:00.000Z' @('Documents/Plans/Test/Reference.md')
	& git.exe -C $primary add --all -- Documents/Plans/Test | Out-Null
	$dependencyValidation = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $dependencyValidation 'validate' 'invalid' 'invalid-plans'
	Assert-True (@($dependencyValidation.notices | Where-Object { $_.code -ceq 'stale-dependency' -and $_.plan -ceq 'Documents/Plans/Test/Stale.md' }).Count -eq 1) 'Missing dependency was not a satisfied stale-edge notice.'
	Assert-True (@($dependencyValidation.diagnostics | Where-Object code -ceq 'dependency-cycle').Count -eq 3) 'Self/cycle component was not quarantined.'
	Commit $primary 'tracked dependency fixtures'
	$claimBaseline = (& git.exe -C $primary rev-parse HEAD).Trim()
	$mismatch = Invoke-Cli 1 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-mismatch','--session','session-mismatch','--write-claim-receipt',(Join-Path $session 'Temp/mismatch.json'))
	Assert-Result $mismatch 'claim-next' 'error' 'git-identity-mismatch'
	& git.exe -C $session reset --hard $claimBaseline | Out-Null

	# Claims select strictly oldest then UTF-8 path, reject a foreign live claim, return same-session retry receipt, and
	# unclaim makes a plan eligible immediately.
	$receipt = Join-Path $session 'Temp/older.json'
	$claim = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-a','--session','session-a','--write-claim-receipt',$receipt)
	Assert-Result $claim 'claim-next' 'ok' 'claimed'; Assert-True ($claim.claimed -and $claim.plan -ceq 'Documents/Plans/Test/Older.md') 'Oldest eligible Plan was not claimed.'
	foreach ($field in @('plan','digest','baseline','receipt','healedClaims')) { Assert-True ($claim.PSObject.Properties.Name -ccontains $field) "Claim result omits '$field'." }
	foreach ($field in @('path','sha256','size')) { Assert-True ($claim.receipt.PSObject.Properties.Name -ccontains $field) "Claim receipt identity omits '$field'." }
	$foreignReceipt = Join-Path $session 'Temp/foreign.json'; $foreign = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-b','--session','session-b','--write-claim-receipt',$foreignReceipt,'--plan','Documents/Plans/Test/Older.md')
	Assert-Result $foreign 'claim-next' 'ok' 'none-available'; Assert-True (-not $foreign.claimed) 'Foreign live claim was stolen.'
	$retryReceipt = Join-Path $session 'Temp/retry.json'; $retry = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-a','--session','session-a','--write-claim-receipt',$retryReceipt,'--plan','Documents/Plans/Test/Older.md')
	Assert-Result $retry 'claim-next' 'ok' 'claimed'; Assert-True ($retry.plan -ceq $claim.plan -and (Test-Path -LiteralPath $retryReceipt)) 'Same-session retry did not regenerate its receipt.'
	$claimTree = (& git.exe -C $session write-tree).Trim(); $rewrittenHead = ('rewritten fixture history' | & git.exe -C $session commit-tree $claimTree).Trim(); & git.exe -C $session reset --hard $rewrittenHead | Out-Null
	$ancestryBroken = Invoke-Cli 2 @('plan','claim-status','--worktree',$session,'--claim-receipt',$receipt,'--claim-receipt-sha256',$claim.receipt.sha256)
	Assert-Result $ancestryBroken 'claim-status' 'conflict' 'receipt-invalid'
	$ancestryHeal = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$session,'--baseline',$claimBaseline)
	Assert-Result $ancestryHeal 'validate' 'invalid' 'invalid-plans'; Assert-True ($ancestryHeal.healedClaims.Count -eq 1) 'Non-ancestor live claim was not healed.'
	& git.exe -C $session reset --hard $claimBaseline | Out-Null
	$reclaimedReceipt = Join-Path $session 'Temp/reclaimed.json'; $reclaimed = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-reclaimed','--session','session-reclaimed','--write-claim-receipt',$reclaimedReceipt,'--plan','Documents/Plans/Test/Older.md')
	Assert-Result $reclaimed 'claim-next' 'ok' 'claimed'; Assert-True $reclaimed.claimed 'Healed non-ancestor claim did not become immediately available.'
	$status = Invoke-Cli 0 @('plan','claim-status','--worktree',$session,'--claim-receipt',$reclaimed.receipt.path,'--claim-receipt-sha256',$reclaimed.receipt.sha256)
	Assert-Result $status 'claim-status' 'ok' 'claimed'; Assert-True ($status.ownedByReceipt -and $status.claimState -ceq 'claimed') 'Receipt does not prove current claim ownership.'
	$unclaim = Invoke-Cli 0 @('plan','unclaim','--worktree',$session,'--claim-receipt',$reclaimed.receipt.path,'--claim-receipt-sha256',$reclaimed.receipt.sha256)
	Assert-Result $unclaim 'unclaim' 'ok' 'released'; Assert-True $unclaim.released 'Deferral did not release the claim.'
	$alreadyAbsent = Invoke-Cli 0 @('plan','unclaim','--worktree',$session,'--claim-receipt',$reclaimed.receipt.path,'--claim-receipt-sha256',$reclaimed.receipt.sha256)
	Assert-Result $alreadyAbsent 'unclaim' 'ok' 'already-absent'
	$timestampReceipt = Join-Path $session 'Temp/timestamp.json'; $timestampClaim = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-timestamp','--session','session-timestamp','--write-claim-receipt',$timestampReceipt,'--plan','Documents/Plans/Test/Older.md')
	$timestampReceiptIdentity = Get-Content -LiteralPath $timestampClaim.receipt.path -Raw | ConvertFrom-Json -Depth 100
	$timestampClaimRecord = Get-Content -LiteralPath $timestampReceiptIdentity.claimPath -Raw | ConvertFrom-Json -Depth 100
	$timestampClaimRecord.claimedAt = '2099-1-002T00:00:00.000Z'; $timestampClaimRecord.expiresAt = '2099-1-004T00:00:00.000Z'
	[IO.File]::SetAttributes($timestampReceiptIdentity.claimPath, [IO.FileAttributes]::Normal)
	[IO.File]::WriteAllText($timestampReceiptIdentity.claimPath, (($timestampClaimRecord | ConvertTo-Json -Depth 100) + "`n"), $utf8)
	$timestampHeal = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$session,'--baseline',$claimBaseline)
	Assert-Result $timestampHeal 'validate' 'invalid' 'invalid-plans'; Assert-True ($timestampHeal.healedClaims.Count -eq 1) 'Noncanonical live claim timestamps passed strict claim validation.'

	# Receipt paths are new regular descendants of Temp: traversal, reparse, and existing target must fail without leaving a
	# claim; an owned claim survives failed retry regeneration. The implementation's storage self-heals bad/expired/orphaned
	# record schema and filenames before evaluating candidates.
	$traversal = Invoke-Cli 1 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-c','--session','session-c','--write-claim-receipt',(Join-Path $session 'outside.json'),'--plan','Documents/Plans/Test/Older.md')
	Assert-Result $traversal 'claim-next' 'error' 'receipt-failed'
	$existingReceipt = Join-Path $session 'Temp/existing.json'; [IO.File]::WriteAllText($existingReceipt, '{}', $utf8)
	$existing = Invoke-Cli 1 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-c','--session','session-c','--write-claim-receipt',$existingReceipt,'--plan','Documents/Plans/Test/Older.md')
	Assert-Result $existing 'claim-next' 'error' 'receipt-failed'
	$reparseRoot = Join-Path $root 'receipt-target'; New-Item -ItemType Directory -Force -Path $reparseRoot | Out-Null
	New-Item -ItemType Junction -Path (Join-Path $session 'Temp/reparse') -Target $reparseRoot | Out-Null
	$reparse = Invoke-Cli 1 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-c','--session','session-c','--write-claim-receipt',(Join-Path $session 'Temp/reparse/receipt.json'),'--plan','Documents/Plans/Test/Older.md')
	Assert-Result $reparse 'claim-next' 'error' 'receipt-failed'
	$afterRollback = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-d','--session','session-d','--write-claim-receipt',(Join-Path $session 'Temp/rollback.json'),'--plan','Documents/Plans/Test/Older.md')
	Assert-True $afterRollback.claimed 'Receipt failure left a new claim behind.'
	Invoke-Cli 0 @('plan','unclaim','--worktree',$session,'--claim-receipt',$afterRollback.receipt.path,'--claim-receipt-sha256',$afterRollback.receipt.sha256) | Out-Null
	$claimsDirectory = Get-ChildItem -LiteralPath $localAppData -Directory -Recurse | Where-Object { $_.Name -ceq 'claims' } | Select-Object -First 1 -ExpandProperty FullName
	Assert-True (-not [string]::IsNullOrWhiteSpace($claimsDirectory)) 'Scheduler did not create isolated claim storage.'
	foreach ($name in @('corrupt.json','schema.json','orphan.json','invalid-name.json')) { [IO.File]::WriteAllText((Join-Path $claimsDirectory $name), '{not-json', $utf8) }
	$healed = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $healed 'validate' 'invalid' 'invalid-plans'; Assert-True ($healed.healedClaims.Count -eq 4) 'Corrupt/schema/orphan/invalid-name claim records were not self-healed.'

	# Completion/rejection are receipt bound. Completion rewrites direct children, deletes only the target, recovers after a
	# restart, and release proves terminal state plus is idempotent after the landing has advanced.
	$terminalReceipt = Join-Path $session 'Temp/terminal.json'; $terminalClaim = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-e','--session','session-e','--write-claim-receipt',$terminalReceipt,'--plan','Documents/Plans/Test/Older.md')
	$unauthorized = Invoke-Cli 1 @('plan','prepare-rejection','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256)
	Assert-Result $unauthorized 'prepare-rejection' 'error' 'authorization-required'
	$prepared = Invoke-Cli 0 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256)
	Assert-Result $prepared 'prepare-completion' 'ok' 'prepared'; Assert-True ($prepared.claimState -ceq 'awaiting-landing' -and $prepared.changedPaths -contains 'Documents/Plans/Test/Older.md' -and $prepared.changedPaths -contains 'Documents/Plans/Test/Dependent.md') 'Completion did not report target deletion and direct child rewrite.'
	$targetAtomicTemp = Join-Path $session 'Documents/Plans/Test/Older.md.tmp.123.1'; [IO.File]::WriteAllText($targetAtomicTemp, 'orphan', $utf8); [IO.File]::SetAttributes($targetAtomicTemp, [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::Temporary)
	$childAtomicTemp = Join-Path $session 'Documents/Plans/Test/Dependent.md.tmp.123.2'; [IO.File]::WriteAllText($childAtomicTemp, 'orphan', $utf8); [IO.File]::SetAttributes($childAtomicTemp, [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::Temporary)
	$unrelatedUntracked = Join-Path $session 'Documents/Plans/Test/Unrelated.untracked'; [IO.File]::WriteAllText($unrelatedUntracked, 'preserve', $utf8)
	$awaitingRetry = Invoke-Cli 0 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256)
	Assert-Result $awaitingRetry 'prepare-completion' 'ok' 'recovered'; Assert-True ($awaitingRetry.changedPaths.Count -eq 0) 'Unchanged awaiting-landing retry incorrectly reported already-applied manifest paths.'
	Assert-True (-not (Test-Path -LiteralPath $targetAtomicTemp) -and -not (Test-Path -LiteralPath $childAtomicTemp)) 'Recognized scheduler atomic temporary sibling survived terminal retry.'
	Assert-True ((Test-Path -LiteralPath $unrelatedUntracked) -and (@(& git.exe -C $session status --porcelain -- Documents/Plans/Test) -match 'Unrelated\.untracked')) 'Scheduler removed or concealed unrelated untracked file.'
	Remove-Item -LiteralPath $unrelatedUntracked -Force
	$receiptIdentity = Get-Content -LiteralPath $terminalClaim.receipt.path -Raw | ConvertFrom-Json -Depth 100
	$storedClaim = Get-Content -LiteralPath $receiptIdentity.claimPath -Raw | ConvertFrom-Json -Depth 100
	$storedClaim.state = 'preparing'; $storedClaim.PSObject.Properties.Remove('changedPaths'); $storedClaim.PSObject.Properties.Remove('manifestDigest')
	[IO.File]::SetAttributes($receiptIdentity.claimPath, [IO.FileAttributes]::Normal)
	[IO.File]::WriteAllText($receiptIdentity.claimPath, (($storedClaim | ConvertTo-Json -Depth 100) + "`n"), $utf8)
	$recovered = Invoke-Cli 0 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256)
	Assert-Result $recovered 'prepare-completion' 'ok' 'recovered'; Assert-True ($recovered.changedPaths -contains 'Documents/Plans/Test/Older.md' -and $recovered.changedPaths -contains 'Documents/Plans/Test/Dependent.md') 'Preparing recovery concealed manifest paths already in after-state.'
	Commit $session 'terminal plan deletion and dependency cleanup'
	$landed = (& git.exe -C $session rev-parse HEAD).Trim()
	& git.exe -C $primary merge --ff-only $landed | Out-Null
	Set-Plan $primary 'Documents/Plans/Test/Older.md' '2024-01-01T00:00:00.000Z'; Commit $primary 'reintroduce terminal target'
	$targetPresent = Invoke-Cli 2 @('plan','release-after-landing','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256,'--landed-commit',$landed)
	Assert-Result $targetPresent 'release-after-landing' 'conflict' 'terminal-state-not-proven'
	Remove-Item -LiteralPath (Join-Path $primary 'Documents/Plans/Test/Older.md') -Force; Commit $primary 'restore terminal primary state'
	$release = Invoke-Cli 0 @('plan','release-after-landing','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256,'--landed-commit',$landed)
	Assert-Result $release 'release-after-landing' 'ok' 'released'; Assert-True ($release.released -and $release.terminalStateVerified) 'Terminal release did not prove terminal state.'
	$releaseRetry = Invoke-Cli 0 @('plan','release-after-landing','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256,'--landed-commit',$landed)
	Assert-Result $releaseRetry 'release-after-landing' 'ok' 'already-released'

	[pscustomobject]@{ schemaVersion = 'broken-engine-plan-scheduler-fixtures/v1'; status = 'pass'; code = 'ok'; cases = @('metadata strictness, canonical timestamps, and immutability','deep tracked paths','baseline deletion, primary-advance notice, and stale-manual dependency classification','targeted validation','deterministic ordering','dependency and cycle quarantine','claim baseline identity, ancestry healing, and canonical timestamps','receipt-bound claims and unclaim','receipt containment and rollback','terminal atomic orphan cleanup, awaiting retry, preparing recovery, and current-primary release proof') } | ConvertTo-Json -Depth 5 -Compress
}
finally { if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Force -Recurse } }

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
	[IO.File]::WriteAllText((Join-Path $primary 'Documents/Plans/Test/AGENTS.md'), '# Directory guidance', $utf8)
	Commit $primary 'baseline'
	$baseline = (& git.exe -C $primary rev-parse HEAD).Trim()
	& git.exe -C $primary worktree add -b fixture-session $session $baseline | Out-Null
	New-Item -ItemType Directory -Force -Path (Join-Path $session 'Temp') | Out-Null
	$valid = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $valid 'validate' 'valid' 'ok'; Assert-True ($valid.plans.Count -eq 3) 'Exempt directory guidance became executable.'
	Assert-True ($valid.plans[0].path -ceq 'Documents/Plans/Test/Older.md') 'Validation did not retain deterministic inventory order.'

	# Metadata boundary: a plan document without a byte-zero marker - including one displaced by a BOM - is a loud
	# invalid-metadata diagnostic naming its exact path rather than a silent skip, while AGENTS.md/CLAUDE.md stay exempt at
	# any depth. Malformed markers and unknown keys are invalid too. A marker cannot be removed after baseline, while a
	# newly created executable Plan is valid.
	$bomPlan = 'Documents/Plans/Test/Bom.md'; $bomPath = Join-Path $primary $bomPlan; [IO.File]::WriteAllText($bomPath, '<!-- broken-engine-plan/v1 {"createdUtc":"2024-01-03T00:00:00.000Z","dependsOn":[]} -->', [Text.UTF8Encoding]::new($true)); Track-Plan $primary $bomPlan
	$bom = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $bom 'validate' 'invalid' 'invalid-plans'; Assert-True (@($bom.diagnostics | Where-Object { $_.plan -ceq $bomPlan -and $_.code -ceq 'invalid-metadata' }).Count -eq 1 -and @($bom.plans | Where-Object { $_.path -ceq $bomPlan }).Count -eq 0) 'BOM-displaced marker was not a loud non-executable diagnostic.'
	Remove-TemporaryPlan $primary $bomPlan
	$nestedGuidance = 'Documents/Plans/Test/Nested/CLAUDE.md'; [IO.Directory]::CreateDirectory((Join-Path $primary 'Documents/Plans/Test/Nested')) | Out-Null; [IO.File]::WriteAllText((Join-Path $primary $nestedGuidance), '@AGENTS.md', $utf8); Track-Plan $primary $nestedGuidance
	$markedGuidance = 'Documents/Plans/Test/Nested/AGENTS.md'; Set-Plan $primary $markedGuidance '2018-01-01T00:00:00.000Z'; Track-Plan $primary $markedGuidance
	$guidancePaths = @($nestedGuidance,$markedGuidance,'Documents/Plans/Test/AGENTS.md')
	$guidance = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $guidance 'validate' 'valid' 'ok'; Assert-True (@($guidance.plans | Where-Object { $_.path -cin $guidancePaths }).Count -eq 0 -and @($guidance.diagnostics | Where-Object { $_.plan -cin $guidancePaths }).Count -eq 0) 'Directory guidance became executable metadata or was reported, marker or not.'
	Remove-TemporaryPlan $primary $nestedGuidance; Remove-TemporaryPlan $primary $markedGuidance
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

	# Baseline deletions are classified by baseline metadata: non-executable files may disappear, while executable Plans
	# require a terminal receipt.
	$guidancePath = Join-Path $primary 'Documents/Plans/Test/AGENTS.md'; Remove-Item -LiteralPath $guidancePath -Force
	$guidanceStalePlan = 'Documents/Plans/Test/GuidanceStale.md'; Set-Plan $primary $guidanceStalePlan '2024-01-03T00:00:00.000Z' @('Documents/Plans/Test/AGENTS.md'); Track-Plan $primary $guidanceStalePlan
	$guidanceDeletion = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $guidanceDeletion 'validate' 'valid' 'ok'; Assert-True (@($guidanceDeletion.plans | Where-Object { $_.path -ceq $guidanceStalePlan }).Count -eq 1 -and @($guidanceDeletion.notices | Where-Object { $_.plan -ceq $guidanceStalePlan -and $_.code -ceq 'stale-dependency' }).Count -eq 1) 'Deleted baseline non-executable dependency did not become a satisfied stale-edge notice.'
	Remove-TemporaryPlan $primary $guidanceStalePlan
	& git.exe -C $primary checkout -- Documents/Plans/Test/AGENTS.md | Out-Null
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
	# An incorporated advance that demoted a Plan leaves a marker-less plan document in the reconciled tree: that document is
	# loud on its own path, while the non-blocking primary-advance notices for both the removal and the demotion still fire.
	$primaryAdvance = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$session,'--baseline',$baseline)
	Assert-Result $primaryAdvance 'validate' 'invalid' 'invalid-plans'
	Assert-True (@($primaryAdvance.diagnostics).Count -eq 1 -and @($primaryAdvance.diagnostics | Where-Object { $_.plan -ceq 'Documents/Plans/Test/Zulu.md' -and $_.code -ceq 'invalid-metadata' }).Count -eq 1) 'Reconciled demotion to marker-less bytes was not the only reported diagnostic.'
	Assert-True (@($primaryAdvance.notices | Where-Object code -ceq 'missing-plan-file').Count -eq 2) 'Reconciled primary Plan removal or demotion lost its non-blocking primary-advance notice.'
	# With the demoted document also removed by the advance, the reconciled tree carries no marker-less plan document, so the
	# removal tolerance is observable on its own: notices only, nothing blocking.
	Remove-Item -LiteralPath (Join-Path $primary 'Documents/Plans/Test/Zulu.md') -Force
	Commit $primary 'primary advance also removes the demoted plan'
	& git.exe -C $session merge --ff-only main | Out-Null
	$primaryAdvanceClean = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$session,'--baseline',$baseline)
	Assert-Result $primaryAdvanceClean 'validate' 'valid' 'ok'; Assert-True (@($primaryAdvanceClean.notices | Where-Object code -ceq 'missing-plan-file').Count -eq 2) 'Reconciled primary Plan removal remained blocking.'
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

	# Dependencies: existing executable nodes block, missing nodes are satisfied notices, invalid/non-executable nodes
	# block, and a self/cycle only quarantines its component while unrelated plan remains claimable. The committed
	# marker-less plan document stays loud for the rest of the run without making anything else unclaimable.
	Set-Plan $primary 'Documents/Plans/Test/Dependent.md' '2024-01-04T00:00:00.000Z' @('Documents/Plans/Test/Older.md')
	Set-Plan $primary 'Documents/Plans/Test/Stale.md' '2024-01-05T00:00:00.000Z' @('Documents/Plans/Test/Deleted.md')
	Set-Plan $primary 'Documents/Plans/Test/Self.md' '2024-01-06T00:00:00.000Z' @('Documents/Plans/Test/Self.md')
	Set-Plan $primary 'Documents/Plans/Test/CycleA.md' '2024-01-07T00:00:00.000Z' @('Documents/Plans/Test/CycleB.md')
	Set-Plan $primary 'Documents/Plans/Test/CycleB.md' '2024-01-08T00:00:00.000Z' @('Documents/Plans/Test/CycleA.md')
	Set-Plan $primary 'Documents/Plans/Test/GuidanceBlocked.md' '2024-01-09T00:00:00.000Z' @('Documents/Plans/Test/AGENTS.md')
	$markerlessPlan = 'Documents/Plans/Test/Markerless.md'; [IO.File]::WriteAllText((Join-Path $primary $markerlessPlan), '# Marker-less plan document', $utf8)
	& git.exe -C $primary add --all -- Documents/Plans/Test | Out-Null
	$dependencyValidation = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$baseline)
	Assert-Result $dependencyValidation 'validate' 'invalid' 'invalid-plans'
	Assert-True (@($dependencyValidation.notices | Where-Object { $_.code -ceq 'stale-dependency' -and $_.plan -ceq 'Documents/Plans/Test/Stale.md' }).Count -eq 1) 'Missing dependency was not a satisfied stale-edge notice.'
	Assert-True (@($dependencyValidation.diagnostics | Where-Object code -ceq 'dependency-cycle').Count -eq 3) 'Self/cycle component was not quarantined.'
	Assert-True (@($dependencyValidation.diagnostics | Where-Object { $_.plan -ceq $markerlessPlan -and $_.code -ceq 'invalid-metadata' }).Count -eq 1 -and @($dependencyValidation.plans | Where-Object { $_.path -ceq $markerlessPlan }).Count -eq 0) 'Marker-less plan document was not reported once by its exact path as non-executable.'
	Commit $primary 'tracked dependency fixtures'
	$claimBaseline = (& git.exe -C $primary rev-parse HEAD).Trim()
	# A session HEAD behind the primary tip (a peer landing advanced primary) is a first-class claim: it records the session
	# HEAD as its baseline and selects from the session tree. A genuinely diverged session (a local commit absent from the
	# primary tip) still refuses.
	$behindHead = (& git.exe -C $session rev-parse HEAD).Trim()
	$behindReceipt = Join-Path $session 'Temp/behind.json'
	$behind = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-behind','--session','session-behind','--write-claim-receipt',$behindReceipt)
	Assert-Result $behind 'claim-next' 'ok' 'claimed'; Assert-True ($behind.claimed -and $behind.baseline -ceq $behindHead) 'Behind-session claim did not record the session HEAD as its baseline.'
	Invoke-Cli 0 @('plan','unclaim','--worktree',$session,'--claim-receipt',$behind.receipt.path,'--claim-receipt-sha256',$behind.receipt.sha256) | Out-Null
	[IO.File]::WriteAllText((Join-Path $session 'Documents/Plans/Test/Diverged.md'), '# diverged', $utf8)
	& git.exe -C $session add -- Documents/Plans/Test/Diverged.md | Out-Null; & git.exe -C $session commit -m 'diverged local work' | Out-Null
	$mismatch = Invoke-Cli 1 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-mismatch','--session','session-mismatch','--write-claim-receipt',(Join-Path $session 'Temp/mismatch.json'))
	Assert-Result $mismatch 'claim-next' 'error' 'git-identity-mismatch'
	& git.exe -C $session reset --hard $claimBaseline | Out-Null

	# The committed marker-less plan document is loud but never claimable, and quarantines only itself: an unrelated valid
	# plan is still claimable while it exists.
	$markerlessTarget = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-quarantine','--session','session-quarantine','--write-claim-receipt',(Join-Path $session 'Temp/markerless-target.json'),'--plan',$markerlessPlan)
	Assert-Result $markerlessTarget 'claim-next' 'ok' 'none-available'; Assert-True (-not $markerlessTarget.claimed) 'Marker-less plan document was claimable.'
	$quarantine = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-quarantine','--session','session-quarantine','--write-claim-receipt',(Join-Path $session 'Temp/markerless-quarantine.json'),'--plan','Documents/Plans/Test/Zulu.md')
	Assert-Result $quarantine 'claim-next' 'ok' 'claimed'; Assert-True ($quarantine.claimed -and $quarantine.plan -ceq 'Documents/Plans/Test/Zulu.md') 'Marker-less plan document blocked an unrelated valid claim.'
	Invoke-Cli 0 @('plan','unclaim','--worktree',$session,'--claim-receipt',$quarantine.receipt.path,'--claim-receipt-sha256',$quarantine.receipt.sha256) | Out-Null

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

	# Recovery after Git has dropped an empty last-Plan parent treats only a missing directory as an empty cleanup set.
	# A non-directory at that same path remains a deterministic cleanup failure.
	$lastPlan = 'Documents/Plans/LastPlanParent/Only.md'
	$lastPlanParent = Join-Path $session 'Documents/Plans/LastPlanParent'
	Set-Plan $primary $lastPlan '2024-01-10T00:00:00.000Z'
	Commit $primary 'last plan parent fixture'
	& git.exe -C $session merge --ff-only main | Out-Null
	$lastReceipt = Join-Path $session 'Temp/last-plan-parent.json'
	$lastClaim = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-last','--session','session-last','--write-claim-receipt',$lastReceipt,'--plan',$lastPlan)
	Assert-Result $lastClaim 'claim-next' 'ok' 'claimed'
	$lastPrepared = Invoke-Cli 0 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$lastClaim.receipt.path,'--claim-receipt-sha256',$lastClaim.receipt.sha256)
	Assert-Result $lastPrepared 'prepare-completion' 'ok' 'prepared'; Assert-True ($lastPrepared.claimState -ceq 'awaiting-landing' -and -not (Test-Path -LiteralPath (Join-Path $session $lastPlan))) 'Last Plan did not reach awaiting-landing deletion state.'
	Remove-Item -LiteralPath $lastPlanParent -Force
	[IO.File]::WriteAllText($lastPlanParent, 'not a directory', $utf8)
	$lastBlocked = Invoke-Cli 1 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$lastClaim.receipt.path,'--claim-receipt-sha256',$lastClaim.receipt.sha256)
	Assert-Result $lastBlocked 'prepare-completion' 'error' 'orphan-cleanup-failed'
	Remove-Item -LiteralPath $lastPlanParent -Force
	$lastRecovered = Invoke-Cli 0 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$lastClaim.receipt.path,'--claim-receipt-sha256',$lastClaim.receipt.sha256)
	Assert-Result $lastRecovered 'prepare-completion' 'ok' 'recovered'; Assert-True ($lastRecovered.claimState -ceq 'awaiting-landing' -and $lastRecovered.changedPaths.Count -eq 0 -and -not (Test-Path -LiteralPath $lastPlanParent)) 'Missing last-Plan parent did not recover as an empty cleanup set.'
	Commit $session 'last plan parent deletion'
	$lastLanded = (& git.exe -C $session rev-parse HEAD).Trim()
	& git.exe -C $primary merge --ff-only $lastLanded | Out-Null
	$lastRelease = Invoke-Cli 0 @('plan','release-after-landing','--repo',$repo,'--worktree',$session,'--claim-receipt',$lastClaim.receipt.path,'--claim-receipt-sha256',$lastClaim.receipt.sha256,'--landed-commit',$lastLanded)
	Assert-Result $lastRelease 'release-after-landing' 'ok' 'released'

	# Completion/rejection are receipt bound. Completion rewrites direct children, deletes only the target, recovers after a
	# restart, and release proves terminal state plus is idempotent after the landing has advanced.
	$terminalReceipt = Join-Path $session 'Temp/terminal.json'; $terminalClaim = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-e','--session','session-e','--write-claim-receipt',$terminalReceipt,'--plan','Documents/Plans/Test/Older.md')
	$unauthorized = Invoke-Cli 1 @('plan','prepare-rejection','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256)
	Assert-Result $unauthorized 'prepare-rejection' 'error' 'authorization-required'
	# Guidance metadata naming the terminal target is inert: it is neither a blocking invalid child nor a rewritten one.
	Set-Plan $session $markedGuidance '2018-01-01T00:00:00.000Z' @('Documents/Plans/Test/Older.md'); Track-Plan $session $markedGuidance
	$prepared = Invoke-Cli 0 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256)
	Assert-Result $prepared 'prepare-completion' 'ok' 'prepared'; Assert-True ($prepared.claimState -ceq 'awaiting-landing' -and $prepared.changedPaths -contains 'Documents/Plans/Test/Older.md' -and $prepared.changedPaths -contains 'Documents/Plans/Test/Dependent.md' -and $prepared.changedPaths -notcontains $markedGuidance) 'Completion did not report target deletion and direct child rewrite, or treated guidance as a child.'
	Remove-TemporaryPlan $session $markedGuidance
	$targetAtomicTemp = Join-Path $session 'Documents/Plans/Test/Older.md.tmp.123.1'; [IO.File]::WriteAllText($targetAtomicTemp, 'orphan', $utf8); [IO.File]::SetAttributes($targetAtomicTemp, [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::Temporary)
	$childAtomicTemp = Join-Path $session 'Documents/Plans/Test/Dependent.md.tmp.123.2'; [IO.File]::WriteAllText($childAtomicTemp, 'orphan', $utf8); [IO.File]::SetAttributes($childAtomicTemp, [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::Temporary)
	$unrelatedUntracked = Join-Path $session 'Documents/Plans/Test/Unrelated.untracked'; [IO.File]::WriteAllText($unrelatedUntracked, 'preserve', $utf8)
	$awaitingRetry = Invoke-Cli 0 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256)
	Assert-Result $awaitingRetry 'prepare-completion' 'ok' 'recovered'; Assert-True ($awaitingRetry.changedPaths.Count -eq 0) 'Unchanged awaiting-landing retry incorrectly reported already-applied manifest paths.'
	Assert-True (-not (Test-Path -LiteralPath $targetAtomicTemp) -and -not (Test-Path -LiteralPath $childAtomicTemp)) 'Recognized scheduler atomic temporary sibling survived terminal retry.'
	Assert-True ((Test-Path -LiteralPath $unrelatedUntracked) -and (@(& git.exe -C $session status --porcelain -- Documents/Plans/Test) -match 'Unrelated\.untracked')) 'Scheduler removed or concealed unrelated untracked file.'
	Remove-Item -LiteralPath $unrelatedUntracked -Force
	# The transaction owns only the byte-zero marker, so a reconciled child body is expected: it must not conflict and must
	# survive the retry. A child whose dependsOn edge is restored is still pending and gets its rewrite reapplied.
	$childDiskPath = Join-Path $session 'Documents/Plans/Test/Dependent.md'
	$childBodyEdit = "`n## Reconciled body`n"
	# The scheduler's atomic rewrite leaves each rewritten Plan hidden+temporary, which blocks WriteAllText.
	[IO.File]::SetAttributes($childDiskPath, [IO.FileAttributes]::Normal)
	[IO.File]::WriteAllText($childDiskPath, ([IO.File]::ReadAllText($childDiskPath) + $childBodyEdit), $utf8)
	$reconciledChild = Invoke-Cli 0 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256)
	Assert-Result $reconciledChild 'prepare-completion' 'ok' 'recovered'
	Assert-True ([IO.File]::ReadAllText($childDiskPath).EndsWith($childBodyEdit)) 'Reconciled child body edit did not survive terminal recovery.'
	# Restore only the byte-zero marker edge: keeping the reconciled body makes these bytes match neither manifest state, so a
	# digest-only classifier conflicts here instead of rewriting.
	$childRewritten = [IO.File]::ReadAllText($childDiskPath)
	$childRestored = '<!-- broken-engine-plan/v1 {"createdUtc":"2024-01-04T00:00:00.000Z","dependsOn":["Documents/Plans/Test/Older.md"]} -->' + $childRewritten.Substring($childRewritten.IndexOf("`n"))
	[IO.File]::SetAttributes($childDiskPath, [IO.FileAttributes]::Normal)
	[IO.File]::WriteAllText($childDiskPath, $childRestored, $utf8)
	$restoredEdge = Invoke-Cli 0 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256)
	Assert-Result $restoredEdge 'prepare-completion' 'ok' 'recovered'
	Assert-True ($restoredEdge.changedPaths -contains 'Documents/Plans/Test/Dependent.md') 'Restored child dependency edge was not reported as rewritten.'
	Assert-True (-not ([IO.File]::ReadAllText($childDiskPath) -match 'Older\.md')) 'Restored child dependency edge was not rewritten on disk.'
	Assert-True ([IO.File]::ReadAllText($childDiskPath).EndsWith($childBodyEdit)) 'Restored-edge rewrite discarded the reconciled child body.'
	# Deletion destroys the whole target file, so third-party bytes there still conflict, and the conflict names the Plan.
	$targetDiskPath = Join-Path $session 'Documents/Plans/Test/Older.md'
	Set-Plan $session 'Documents/Plans/Test/Older.md' '2024-01-01T00:00:00.000Z'
	[IO.File]::WriteAllText($targetDiskPath, ([IO.File]::ReadAllText($targetDiskPath) + "## Third-party edit`n"), $utf8)
	$targetConflict = Invoke-Cli 2 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256)
	Assert-Result $targetConflict 'prepare-completion' 'conflict' 'recovery-conflict'
	Assert-True (($targetConflict.PSObject.Properties.Name -ccontains 'plan') -and $targetConflict.plan -ceq 'Documents/Plans/Test/Older.md' -and $targetConflict.message -ceq 'target plan has third-party bytes') 'Target third-party bytes did not conflict by name.'
	Remove-Item -LiteralPath $targetDiskPath -Force
	$receiptIdentity = Get-Content -LiteralPath $terminalClaim.receipt.path -Raw | ConvertFrom-Json -Depth 100
	$storedClaim = Get-Content -LiteralPath $receiptIdentity.claimPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
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
	# An absent terminal target is idempotent only when the scheduler can prove every local record is harmless. A malformed
	# target, a non-regular target, and malformed/non-regular competitor records all fail closed without retiring the receipt
	# or reporting receipt identity details.
	$terminalReceiptBytes = [IO.File]::ReadAllBytes($terminalClaim.receipt.path)
	$terminalReceiptIdentity = Get-Content -LiteralPath $terminalClaim.receipt.path -Raw | ConvertFrom-Json -Depth 100
	$terminalClaimPath = [string]$terminalReceiptIdentity.claimPath
	Assert-True (-not (Test-Path -LiteralPath $terminalClaimPath)) 'Released terminal claim path is absent before absence-proof cases.'
	[IO.File]::WriteAllText($terminalClaimPath, '{not-json', $utf8)
	$malformedTargetBytes = [IO.File]::ReadAllBytes($terminalClaimPath)
	$malformedTarget = Invoke-Cli 2 @('plan','release-after-landing','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256,'--landed-commit',$landed)
	Assert-Result $malformedTarget 'release-after-landing' 'conflict' 'target-claim-unproven'
	Assert-True ((Test-Path -LiteralPath $terminalClaimPath) -and ([Convert]::ToHexString([IO.File]::ReadAllBytes($terminalClaimPath)) -ceq [Convert]::ToHexString($malformedTargetBytes)) -and ((($malformedTarget | ConvertTo-Json -Depth 20 -Compress) -notmatch 'Older\.md|claimPath|sha256|receipt'))) 'Malformed target claim changed, was deleted, or leaked receipt identity.'
	Remove-Item -LiteralPath $terminalClaimPath -Force
	New-Item -ItemType Directory -Path $terminalClaimPath | Out-Null
	$directoryTarget = Invoke-Cli 2 @('plan','release-after-landing','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256,'--landed-commit',$landed)
	Assert-Result $directoryTarget 'release-after-landing' 'conflict' 'target-claim-unproven'
	Assert-True ((Test-Path -LiteralPath $terminalClaimPath -PathType Container) -and ((($directoryTarget | ConvertTo-Json -Depth 20 -Compress) -notmatch 'Older\.md|claimPath|sha256|receipt'))) 'Non-regular target claim was changed, deleted, or leaked receipt identity.'
	Remove-Item -LiteralPath $terminalClaimPath -Force -Recurse
	$malformedCompetitorPath = Join-Path $claimsDirectory 'release-unrelated-malformed.json'
	[IO.File]::WriteAllText($malformedCompetitorPath, '{not-json', $utf8)
	$malformedCompetitorBytes = [IO.File]::ReadAllBytes($malformedCompetitorPath)
	$malformedCompetitor = Invoke-Cli 2 @('plan','release-after-landing','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256,'--landed-commit',$landed)
	Assert-Result $malformedCompetitor 'release-after-landing' 'conflict' 'competitor-scan-unproven'
	Assert-True ((Test-Path -LiteralPath $malformedCompetitorPath) -and ([Convert]::ToHexString([IO.File]::ReadAllBytes($malformedCompetitorPath)) -ceq [Convert]::ToHexString($malformedCompetitorBytes)) -and ((($malformedCompetitor | ConvertTo-Json -Depth 20 -Compress) -notmatch 'Older\.md|claimPath|sha256|receipt'))) 'Malformed competitor changed, was deleted, or leaked receipt identity.'
	Remove-Item -LiteralPath $malformedCompetitorPath -Force
	$directoryCompetitorPath = Join-Path $claimsDirectory 'release-unrelated-directory.json'
	New-Item -ItemType Directory -Path $directoryCompetitorPath | Out-Null
	$directoryCompetitor = Invoke-Cli 2 @('plan','release-after-landing','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256,'--landed-commit',$landed)
	Assert-Result $directoryCompetitor 'release-after-landing' 'conflict' 'competitor-scan-unproven'
	Assert-True ((Test-Path -LiteralPath $directoryCompetitorPath -PathType Container) -and ((($directoryCompetitor | ConvertTo-Json -Depth 20 -Compress) -notmatch 'Older\.md|claimPath|sha256|receipt'))) 'Non-regular competitor was changed, deleted, or leaked receipt identity.'
	Remove-Item -LiteralPath $directoryCompetitorPath -Force -Recurse

	# A different live terminal claim with the same session identity blocks stale receipt recovery even though the old target is
	# absent. The new receipt remains owned and recoverable; once it releases, unrelated claimed and expired records are inert.
	& git.exe -C $session merge --ff-only main | Out-Null
	$competingReceipt = Join-Path $session 'Temp/competing-terminal.json'
	$competingClaim = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-e','--session','session-e','--write-claim-receipt',$competingReceipt,'--plan','Documents/Plans/Test/Alpha.md')
	Assert-Result $competingClaim 'claim-next' 'ok' 'claimed'
	$competingPrepared = Invoke-Cli 0 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$competingClaim.receipt.path,'--claim-receipt-sha256',$competingClaim.receipt.sha256)
	Assert-Result $competingPrepared 'prepare-completion' 'ok' 'prepared'
	Commit $session 'competing terminal Plan deletion'
	$competingLanded = (& git.exe -C $session rev-parse HEAD).Trim()
	& git.exe -C $primary merge --ff-only $competingLanded | Out-Null
	[IO.File]::WriteAllBytes($terminalClaim.receipt.path, $terminalReceiptBytes)
	$terminalCompetitor = Invoke-Cli 2 @('plan','release-after-landing','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256,'--landed-commit',$competingLanded)
	Assert-Result $terminalCompetitor 'release-after-landing' 'conflict' 'competing-terminal-claim'
	Assert-True (([Convert]::ToHexString([IO.File]::ReadAllBytes($terminalClaim.receipt.path)) -ceq [Convert]::ToHexString($terminalReceiptBytes)) -and ((($terminalCompetitor | ConvertTo-Json -Depth 20 -Compress) -notmatch 'Alpha\.md|Older\.md|claimPath|sha256|receipt'))) 'Competing terminal claim retired or leaked a Plan or receipt identity.'
	$competingStatus = Invoke-Cli 0 @('plan','claim-status','--worktree',$session,'--claim-receipt',$competingClaim.receipt.path,'--claim-receipt-sha256',$competingClaim.receipt.sha256)
	Assert-Result $competingStatus 'claim-status' 'ok' 'claimed'; Assert-True ($competingStatus.ownedByReceipt -and $competingStatus.claimState -ceq 'awaiting-landing') 'Competing terminal claim was not retained as its own live receipt.'
	$competingRecovery = Invoke-Cli 0 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$competingClaim.receipt.path,'--claim-receipt-sha256',$competingClaim.receipt.sha256)
	Assert-Result $competingRecovery 'prepare-completion' 'ok' 'recovered'
	$competingRelease = Invoke-Cli 0 @('plan','release-after-landing','--repo',$repo,'--worktree',$session,'--claim-receipt',$competingClaim.receipt.path,'--claim-receipt-sha256',$competingClaim.receipt.sha256,'--landed-commit',$competingLanded)
	Assert-Result $competingRelease 'release-after-landing' 'ok' 'released'
	Assert-True (@(Get-ChildItem -LiteralPath $claimsDirectory -Force).Count -eq 0) 'Competing terminal release did not leave an empty claims directory.'
	Remove-Item -LiteralPath $claimsDirectory -Force
	$missingClaimsDirectory = Invoke-Cli 0 @('plan','release-after-landing','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256,'--landed-commit',$competingLanded)
	Assert-Result $missingClaimsDirectory 'release-after-landing' 'ok' 'already-released'
	Assert-True (-not (Test-Path -LiteralPath $claimsDirectory)) 'Missing claims directory was recreated by stale receipt recovery.'
	$unrelatedReceipt = Join-Path $session 'Temp/unrelated-release.json'
	$unrelatedClaim = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-release-unrelated','--session','session-release-unrelated','--write-claim-receipt',$unrelatedReceipt,'--plan','Documents/Plans/Test/Zulu.md')
	Assert-Result $unrelatedClaim 'claim-next' 'ok' 'claimed'
	$ordinaryCompetitor = Invoke-Cli 0 @('plan','release-after-landing','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256,'--landed-commit',$competingLanded)
	Assert-Result $ordinaryCompetitor 'release-after-landing' 'ok' 'already-released'
	Assert-True ((($ordinaryCompetitor | ConvertTo-Json -Depth 20 -Compress) -notmatch 'Zulu\.md|claimPath|sha256|receipt')) 'Ordinary unrelated claim leaked claim identity during stale recovery.'
	$unrelatedIdentity = Get-Content -LiteralPath $unrelatedClaim.receipt.path -Raw | ConvertFrom-Json -Depth 100
	$unrelatedRecord = Get-Content -LiteralPath $unrelatedIdentity.claimPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
	$unrelatedRecord.claimedAt = '2020-01-01T00:00:00.000Z'; $unrelatedRecord.expiresAt = '2020-01-03T00:00:00.000Z'
	[IO.File]::SetAttributes($unrelatedIdentity.claimPath, [IO.FileAttributes]::Normal)
	[IO.File]::WriteAllText($unrelatedIdentity.claimPath, (($unrelatedRecord | ConvertTo-Json -Depth 100) + "`n"), $utf8)
	$harmlessCompetitors = Invoke-Cli 0 @('plan','release-after-landing','--repo',$repo,'--worktree',$session,'--claim-receipt',$terminalClaim.receipt.path,'--claim-receipt-sha256',$terminalClaim.receipt.sha256,'--landed-commit',$competingLanded)
	Assert-Result $harmlessCompetitors 'release-after-landing' 'ok' 'already-released'
	Assert-True ((($harmlessCompetitors | ConvertTo-Json -Depth 20 -Compress) -notmatch 'Zulu\.md|claimPath|sha256|receipt')) 'Harmless competitor scan leaked claim identity.'
	Remove-Item -LiteralPath $unrelatedIdentity.claimPath -Force

	# Re-parent after a primary squash. `git reset --soft <root> && git commit` orphans the old baseline, the
	# wrapper `git rebase --onto <new> <old> <branch>` replays session commits, and `plan reparent-claims` moves
	# every live claim (any state) and its Temp receipts onto the new tip. Only after the rebase completes may
	# validate/claim-next run without heal-deleting the reparented claim; mid-conflict they still delete it.
	function Get-FileSha([string] $Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
	function New-ReparentClaim([string] $Sess, [string] $Branch, [string] $Owner, [string] $Plan, [string] $ReceiptName) {
		$claimReceipt = Join-Path $Sess "Temp/$ReceiptName"
		$c = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$Sess,'--branch',$Branch,'--owner',$Owner,'--session',"$Owner-session",'--write-claim-receipt',$claimReceipt,'--plan',$Plan)
		Assert-Result $c 'claim-next' 'ok' 'claimed'; Assert-True $c.claimed "Reparent fixture claim '$Plan' was not granted."
		return $c
	}
	$reparentDir = 'Documents/Plans/Reparent'
	Set-Plan $primary "$reparentDir/Claimed.md" '2024-02-01T00:00:00.000Z'
	Set-Plan $primary "$reparentDir/Preparing.md" '2024-02-02T00:00:00.000Z'
	Set-Plan $primary "$reparentDir/Awaiting.md" '2024-02-03T00:00:00.000Z'
	Set-Plan $primary "$reparentDir/Foreign.md" '2024-02-04T00:00:00.000Z'
	Commit $primary 'reparent baseline plans'
	$reparentOld = (& git.exe -C $primary rev-parse HEAD).Trim()
	$reparentRoot = (& git.exe -C $primary rev-list --max-parents=0 HEAD).Trim()
	& git.exe -C $session reset --hard $reparentOld | Out-Null
	$session2 = Join-Path $root 'session2'
	& git.exe -C $primary worktree add -b fixture-session-2 $session2 $reparentOld | Out-Null
	New-Item -ItemType Directory -Force -Path (Join-Path $session2 'Temp') | Out-Null

	$claimedClaim = New-ReparentClaim $session 'fixture-session' 'owner-reparent-claimed' "$reparentDir/Claimed.md" 'reparent-claimed.json'
	$preparingClaim = New-ReparentClaim $session 'fixture-session' 'owner-reparent-preparing' "$reparentDir/Preparing.md" 'reparent-preparing.json'
	$awaitingClaim = New-ReparentClaim $session 'fixture-session' 'owner-reparent-awaiting' "$reparentDir/Awaiting.md" 'reparent-awaiting.json'
	$foreignClaim = New-ReparentClaim $session2 'fixture-session-2' 'owner-reparent-foreign' "$reparentDir/Foreign.md" 'reparent-foreign.json'
	$foreignClaimPath = (Get-Content -LiteralPath $foreignClaim.receipt.path -Raw | ConvertFrom-Json -Depth 100).claimPath

	# Drive two claims into their live non-claimed states, then restore the deleted plan bytes so the rebase
	# replays no session commit. The preparing claim is a real awaiting-landing record downgraded to 14 fields.
	Invoke-Cli 0 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$preparingClaim.receipt.path,'--claim-receipt-sha256',$preparingClaim.receipt.sha256) | Out-Null
	Invoke-Cli 0 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$awaitingClaim.receipt.path,'--claim-receipt-sha256',$awaitingClaim.receipt.sha256) | Out-Null
	& git.exe -C $session checkout -- "$reparentDir/Preparing.md" "$reparentDir/Awaiting.md" | Out-Null
	$prepIdentity = Get-Content -LiteralPath $preparingClaim.receipt.path -Raw | ConvertFrom-Json -Depth 100
	$prepRecord = Get-Content -LiteralPath $prepIdentity.claimPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
	$prepRecord.state = 'preparing'; $prepRecord.PSObject.Properties.Remove('changedPaths'); $prepRecord.PSObject.Properties.Remove('manifestDigest')
	[IO.File]::SetAttributes($prepIdentity.claimPath, [IO.FileAttributes]::Normal)
	[IO.File]::WriteAllText($prepIdentity.claimPath, (($prepRecord | ConvertTo-Json -Depth 100) + "`n"), $utf8)

	& git.exe -C $primary reset --soft $reparentRoot | Out-Null; & git.exe -C $primary commit -m 'squashed reparent day' | Out-Null
	$reparentNew = (& git.exe -C $primary rev-parse HEAD).Trim()
	& git.exe -C $primary merge-base --is-ancestor $reparentOld $reparentNew *> $null; Assert-True ($LASTEXITCODE -ne 0) 'Squash left the old baseline reachable from the new tip.'
	& git.exe -C $session rebase --onto $reparentNew $reparentOld fixture-session *> $null; Assert-True ($LASTEXITCODE -eq 0) 'Clean re-parent rebase failed.'
	Assert-True ((& git.exe -C $session rev-parse HEAD).Trim() -ceq $reparentNew) 'Re-parented session HEAD is not the squashed tip.'

	$reparent = Invoke-Cli 0 @('plan','reparent-claims','--repo',$repo,'--worktree',$session,'--new-baseline',$reparentNew)
	Assert-Result $reparent 'reparent-claims' 'ok' 'reparented'
	foreach ($plan in @("$reparentDir/Claimed.md","$reparentDir/Preparing.md","$reparentDir/Awaiting.md")) { Assert-True (@($reparent.reparentedClaims) -ccontains $plan) "Live claim '$plan' was not reparented." }
	Assert-True (-not (@($reparent.reparentedClaims) -ccontains "$reparentDir/Foreign.md")) 'Foreign-worktree claim was reparented.'
	Assert-True (@($reparent.updatedReceipts).Count -eq 3) 'Reparent did not rewrite exactly the three session claim receipts.'
	$updatedShas = @($reparent.updatedReceipts | ForEach-Object { [string]$_.sha256 })
	$claimedNewSha = Get-FileSha $claimedClaim.receipt.path
	$preparingNewSha = Get-FileSha $preparingClaim.receipt.path
	$awaitingNewSha = Get-FileSha $awaitingClaim.receipt.path
	foreach ($pair in @(@($claimedClaim.receipt.sha256,$claimedNewSha),@($preparingClaim.receipt.sha256,$preparingNewSha),@($awaitingClaim.receipt.sha256,$awaitingNewSha))) {
		Assert-True ($pair[1] -cne $pair[0]) 'Reparent did not rewrite a claim receipt to a new sha256.'
		Assert-True ($updatedShas -ccontains $pair[1]) 'Reparent updatedReceipts omitted a rewritten receipt sha256.'
	}

	# A completed rebase makes the old baseline unreachable but the new tip an ancestor of the worktree HEAD,
	# so HealClaims (run by validate and by claim-next) must keep every reparented claim.
	$postValidate = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$reparentNew)
	foreach ($claimFile in @($claimedClaim,$preparingClaim,$awaitingClaim | ForEach-Object { Split-Path -Leaf ((Get-Content -LiteralPath $_.receipt.path -Raw | ConvertFrom-Json -Depth 100).claimPath) })) {
		Assert-True (-not (@($postValidate.healedClaims) -ccontains $claimFile)) 'A reparented claim was heal-deleted by a post-rebase validate.'
	}
	$claimedStatus = Invoke-Cli 0 @('plan','claim-status','--worktree',$session,'--claim-receipt',$claimedClaim.receipt.path,'--claim-receipt-sha256',$claimedNewSha)
	Assert-Result $claimedStatus 'claim-status' 'ok' 'claimed'; Assert-True ($claimedStatus.ownedByReceipt -and $claimedStatus.claimState -ceq 'claimed') 'Reparented claimed-state claim lost receipt ownership after validate.'
	$preparingStatus = Invoke-Cli 0 @('plan','claim-status','--worktree',$session,'--claim-receipt',$preparingClaim.receipt.path,'--claim-receipt-sha256',$preparingNewSha)
	Assert-Result $preparingStatus 'claim-status' 'ok' 'claimed'; Assert-True ($preparingStatus.ownedByReceipt -and $preparingStatus.claimState -ceq 'preparing') 'Reparented preparing-state claim lost receipt ownership after validate.'
	$awaitingStatus = Invoke-Cli 0 @('plan','claim-status','--worktree',$session,'--claim-receipt',$awaitingClaim.receipt.path,'--claim-receipt-sha256',$awaitingNewSha)
	Assert-Result $awaitingStatus 'claim-status' 'ok' 'claimed'; Assert-True ($awaitingStatus.ownedByReceipt -and $awaitingStatus.claimState -ceq 'awaiting-landing') 'Reparented awaiting-landing claim lost receipt ownership after validate.'
	$staleStatus = Invoke-Cli 2 @('plan','claim-status','--worktree',$session,'--claim-receipt',$claimedClaim.receipt.path,'--claim-receipt-sha256',$claimedClaim.receipt.sha256)
	Assert-Result $staleStatus 'claim-status' 'conflict' 'receipt-invalid'
	$healProbe = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-reparent-probe','--session','session-reparent-probe','--write-claim-receipt',(Join-Path $session 'Temp/reparent-probe.json'),'--plan',"$reparentDir/Claimed.md")
	Assert-Result $healProbe 'claim-next' 'ok' 'none-available'; Assert-True (-not $healProbe.claimed) 'claim-next heal-deleted and stole a reparented live claim.'
	$claimedAfterNext = Invoke-Cli 0 @('plan','claim-status','--worktree',$session,'--claim-receipt',$claimedClaim.receipt.path,'--claim-receipt-sha256',$claimedNewSha)
	Assert-True ($claimedAfterNext.ownedByReceipt -and $claimedAfterNext.claimState -ceq 'claimed') 'claim-next HealClaims discarded a reparented claim.'

	# A claim bound to another worktree at the same old baseline is left untouched.
	$foreignRecord = Get-Content -LiteralPath $foreignClaimPath -Raw | ConvertFrom-Json -Depth 100
	Assert-True ($foreignRecord.primaryCommit -ceq $reparentOld) 'Reparent rewrote a claim bound to a different worktree.'

	# Every live session claim already carries the new baseline (nothing orphaned from the worktree HEAD), so a
	# rerun reparents nothing.
	$noClaims = Invoke-Cli 0 @('plan','reparent-claims','--repo',$repo,'--worktree',$session,'--new-baseline',$reparentNew)
	Assert-Result $noClaims 'reparent-claims' 'ok' 'no-claims'; Assert-True (@($noClaims.reparentedClaims).Count -eq 0 -and @($noClaims.updatedReceipts).Count -eq 0) 'no-claims reparent reported mutations.'

	# Completion and release still work end to end against the landed squashed history.
	$reparentedComplete = Invoke-Cli 0 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$claimedClaim.receipt.path,'--claim-receipt-sha256',$claimedNewSha)
	Assert-Result $reparentedComplete 'prepare-completion' 'ok' 'prepared'; Assert-True ($reparentedComplete.claimState -ceq 'awaiting-landing') 'Reparented claim did not prepare for landing.'
	Commit $session 'land reparented terminal on squashed history'
	$reparentLanded = (& git.exe -C $session rev-parse HEAD).Trim()
	& git.exe -C $primary merge --ff-only $reparentLanded | Out-Null
	$reparentRelease = Invoke-Cli 0 @('plan','release-after-landing','--repo',$repo,'--worktree',$session,'--claim-receipt',$claimedClaim.receipt.path,'--claim-receipt-sha256',$claimedNewSha,'--landed-commit',$reparentLanded)
	Assert-Result $reparentRelease 'release-after-landing' 'ok' 'released'; Assert-True ($reparentRelease.released -and $reparentRelease.terminalStateVerified) 'Reparented claim did not release against the squashed landed history.'

	# Mid-conflict re-parent: the claim reparents while the rebase is stopped, but a validate run mid-conflict
	# heal-deletes it (detached HEAD -> unresolved branch), documenting the prohibition [PA-F-001].
	Set-Plan $primary "$reparentDir/Conflict.md" '2024-02-05T00:00:00.000Z'
	[IO.File]::WriteAllText((Join-Path $primary 'Documents/ReparentConflict.txt'), "base`n", $utf8)
	Commit $primary 'conflict re-parent baseline'
	$conflictOld = (& git.exe -C $primary rev-parse HEAD).Trim()
	$conflictRoot = (& git.exe -C $primary rev-list --max-parents=0 HEAD).Trim()
	& git.exe -C $session reset --hard $conflictOld | Out-Null
	$conflictClaim = New-ReparentClaim $session 'fixture-session' 'owner-reparent-conflict' "$reparentDir/Conflict.md" 'reparent-conflict.json'
	$conflictClaimFile = Split-Path -Leaf ((Get-Content -LiteralPath $conflictClaim.receipt.path -Raw | ConvertFrom-Json -Depth 100).claimPath)
	[IO.File]::WriteAllText((Join-Path $session 'Documents/ReparentConflict.txt'), "session edit`n", $utf8)
	& git.exe -C $session commit -aqm 'session divergent conflict edit' | Out-Null
	[IO.File]::WriteAllText((Join-Path $primary 'Documents/ReparentConflict.txt'), "squashed edit`n", $utf8)
	& git.exe -C $primary add -- Documents/ReparentConflict.txt | Out-Null
	& git.exe -C $primary reset --soft $conflictRoot | Out-Null; & git.exe -C $primary commit -m 'squashed conflict day' | Out-Null
	$conflictNew = (& git.exe -C $primary rev-parse HEAD).Trim()
	& git.exe -C $session rebase --onto $conflictNew $conflictOld fixture-session *> $null
	Assert-True ($LASTEXITCODE -ne 0) 'Divergent re-parent rebase did not conflict.'
	$conflictGitDir = (& git.exe -C $session rev-parse --path-format=absolute --git-dir).Trim()
	Assert-True (Test-Path -LiteralPath (Join-Path $conflictGitDir 'rebase-merge')) 'Conflicted rebase left no rebase-merge state.'
	$conflictReparent = Invoke-Cli 0 @('plan','reparent-claims','--repo',$repo,'--worktree',$session,'--new-baseline',$conflictNew)
	Assert-Result $conflictReparent 'reparent-claims' 'ok' 'reparented'; Assert-True (@($conflictReparent.reparentedClaims) -ccontains "$reparentDir/Conflict.md") 'Mid-conflict re-parent did not move the stopped-rebase claim.'
	$midConflictValidate = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$conflictNew)
	Assert-True (@($midConflictValidate.healedClaims) -ccontains $conflictClaimFile) 'Mid-conflict validate did not heal-delete the reparented claim (prohibition hazard not demonstrated).'
	& git.exe -C $session rebase --abort *> $null

	# The session resets onto the advanced primary tip before claiming, so claim-next stamps that advanced
	# commit (the session HEAD, an ancestor of the primary tip) - newer than the wrapper receipt baseline.
	# A squash orphans that newer commit too, so reparent-claims must select the claims healing would delete
	# (primaryCommit no longer an ancestor of the worktree HEAD), not a fixed old baseline that would miss it.
	$advanceDir = 'Documents/Plans/ReparentAdvance'
	Set-Plan $primary "$advanceDir/Advanced.md" '2024-03-01T00:00:00.000Z'
	Commit $primary 'reparent-advance baseline'
	$advanceB0 = (& git.exe -C $primary rev-parse HEAD).Trim()
	$advanceRoot = (& git.exe -C $primary rev-list --max-parents=0 HEAD).Trim()
	$advanceSession = Join-Path $root 'advance-session'
	& git.exe -C $primary worktree add -b fixture-advance $advanceSession $advanceB0 | Out-Null
	New-Item -ItemType Directory -Force -Path (Join-Path $advanceSession 'Temp') | Out-Null
	# The session is baselined at B0; primary then advances to B1 (a routine land) before the claim is taken.
	Set-Plan $primary "$advanceDir/Later.md" '2024-03-02T00:00:00.000Z'
	Commit $primary 'reparent-advance primary advance'
	$advanceB1 = (& git.exe -C $primary rev-parse HEAD).Trim()
	Assert-True ($advanceB1 -cne $advanceB0) 'Primary advance produced no new commit.'
	& git.exe -C $advanceSession reset --hard $advanceB1 | Out-Null
	$advanceClaim = New-ReparentClaim $advanceSession 'fixture-advance' 'owner-reparent-advance' "$advanceDir/Advanced.md" 'reparent-advance.json'
	$advanceClaimPath = (Get-Content -LiteralPath $advanceClaim.receipt.path -Raw | ConvertFrom-Json -Depth 100).claimPath
	$advanceRecord = Get-Content -LiteralPath $advanceClaimPath -Raw | ConvertFrom-Json -Depth 100
	Assert-True ($advanceRecord.primaryCommit -ceq $advanceB1) 'Claim taken after a primary advance did not stamp the advanced primary commit.'
	$advanceClaimFile = Split-Path -Leaf $advanceClaimPath
	# A session-only commit so the re-parent replays a real commit; then squash primary, orphaning B0 and B1.
	[IO.File]::WriteAllText((Join-Path $advanceSession 'Documents/AdvanceSession.txt'), "session work`n", $utf8)
	& git.exe -C $advanceSession add -- Documents/AdvanceSession.txt | Out-Null
	& git.exe -C $advanceSession commit -qm 'advance session commit' | Out-Null
	& git.exe -C $primary reset --soft $advanceRoot | Out-Null; & git.exe -C $primary commit -m 'squashed reparent-advance day' | Out-Null
	$advanceNew = (& git.exe -C $primary rev-parse HEAD).Trim()
	& git.exe -C $primary merge-base --is-ancestor $advanceB1 $advanceNew *> $null; Assert-True ($LASTEXITCODE -ne 0) 'Squash left the advanced claim commit reachable from the new tip.'
	& git.exe -C $advanceSession rebase --onto $advanceNew $advanceB0 fixture-advance *> $null; Assert-True ($LASTEXITCODE -eq 0) 'Advance re-parent rebase failed.'
	$advanceSessionHead = (& git.exe -C $advanceSession rev-parse HEAD).Trim()
	& git.exe -C $advanceSession merge-base --is-ancestor $advanceB1 $advanceSessionHead *> $null; Assert-True ($LASTEXITCODE -ne 0) 'Advanced claim commit remained an ancestor of the re-parented HEAD.'
	# The live claim carries B1 (newer than the receipt baseline B0); a fixed-baseline selection would miss it.
	$advanceReparent = Invoke-Cli 0 @('plan','reparent-claims','--repo',$repo,'--worktree',$advanceSession,'--new-baseline',$advanceNew)
	Assert-Result $advanceReparent 'reparent-claims' 'ok' 'reparented'
	Assert-True (@($advanceReparent.reparentedClaims) -ccontains "$advanceDir/Advanced.md") 'reparent-claims missed a claim stamped at the advanced primary commit.'
	Assert-True (@($advanceReparent.updatedReceipts).Count -eq 1) 'reparent-claims did not rewrite the advanced claim receipt.'
	$advanceNewSha = Get-FileSha $advanceClaim.receipt.path
	Assert-True ($advanceNewSha -cne $advanceClaim.receipt.sha256) 'Advanced claim receipt was not rewritten to a new sha256.'
	Assert-True (@($advanceReparent.updatedReceipts | ForEach-Object { [string]$_.sha256 }) -ccontains $advanceNewSha) 'reparent-claims updatedReceipts omitted the rewritten advanced receipt sha256.'
	Assert-True ((Get-Content -LiteralPath $advanceClaimPath -Raw | ConvertFrom-Json -Depth 100).primaryCommit -ceq $advanceNew) 'Reparented advanced claim did not adopt the new baseline.'
	# validate runs HealClaims (the identical path claim-next uses) and must keep the reparented claim.
	$advanceValidate = Invoke-Cli 0 @('plan','validate','--repo',$repo,'--worktree',$primary,'--baseline',$advanceNew)
	Assert-True (-not (@($advanceValidate.healedClaims) -ccontains $advanceClaimFile)) 'Reparented advanced claim was heal-deleted by validate.'
	$advanceStatus = Invoke-Cli 0 @('plan','claim-status','--worktree',$advanceSession,'--claim-receipt',$advanceClaim.receipt.path,'--claim-receipt-sha256',$advanceNewSha)
	Assert-Result $advanceStatus 'claim-status' 'ok' 'claimed'; Assert-True ($advanceStatus.ownedByReceipt -and $advanceStatus.claimState -ceq 'claimed') 'Reparented advanced claim lost receipt ownership after validate.'
	# claim-next's HealClaims keeps it too, once primary fast-forwards onto the re-parented session tip.
	& git.exe -C $primary merge --ff-only $advanceSessionHead | Out-Null
	$advanceNextProbe = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$advanceSession,'--branch','fixture-advance','--owner','owner-reparent-advance-probe','--session','session-reparent-advance-probe','--write-claim-receipt',(Join-Path $advanceSession 'Temp/reparent-advance-probe.json'),'--plan',"$advanceDir/Advanced.md")
	Assert-Result $advanceNextProbe 'claim-next' 'ok' 'none-available'; Assert-True (-not $advanceNextProbe.claimed) 'claim-next heal-deleted and stole the reparented advanced claim.'
	$advanceAfterNext = Invoke-Cli 0 @('plan','claim-status','--worktree',$advanceSession,'--claim-receipt',$advanceClaim.receipt.path,'--claim-receipt-sha256',$advanceNewSha)
	Assert-True ($advanceAfterNext.ownedByReceipt -and $advanceAfterNext.claimState -ceq 'claimed') 'claim-next HealClaims discarded the reparented advanced claim.'

	# Reconcile fixture state between the reparent block above and the behind-session cases below. The reparent block
	# leaves the session branch on a divergent conflict-abort tip and live claim records, but the behind-session cases
	# assume a session that fast-forwards onto main and an empty claim store (their one-claim-per-session assertion
	# counts exactly one record). Reset the session onto main and clear leftover claim records to restore that slate.
	& git.exe -C $session reset --hard main | Out-Null
	Get-ChildItem -LiteralPath $claimsDirectory -File -Force -Filter '*.json' | Remove-Item -Force

	# Behind-session reconciliation: selection reads the session tree while healing judges the primary tip. Build a divergence
	# where the session HEAD (an older primary commit) still carries plans the primary tip has since dropped or demoted.
	New-Item -ItemType Directory -Force -Path (Join-Path $primary 'Temp') | Out-Null
	Set-Plan $primary 'Documents/Plans/Test/SessionKept.md' '2020-01-01T00:00:00.000Z'
	Set-Plan $primary 'Documents/Plans/Test/DriftPrereq.md' '2020-01-02T00:00:00.000Z'
	Set-Plan $primary 'Documents/Plans/Test/DriftChild.md' '2020-01-03T00:00:00.000Z' @('Documents/Plans/Test/DriftPrereq.md')
	Commit $primary 'session-tree plans before peer landing'
	& git.exe -C $session merge --ff-only main | Out-Null
	$sessionTree = (& git.exe -C $session rev-parse HEAD).Trim()
	Remove-Item -LiteralPath (Join-Path $primary 'Documents/Plans/Test/SessionKept.md') -Force
	Remove-Item -LiteralPath (Join-Path $primary 'Documents/Plans/Test/DriftPrereq.md') -Force
	Set-Plan $primary 'Documents/Plans/Test/PrimaryOnly.md' '2020-01-04T00:00:00.000Z'
	Commit $primary 'peer landing drops session-tree plans and adds a primary-only plan'
	Assert-True ($sessionTree -cne (& git.exe -C $primary rev-parse HEAD).Trim()) 'Session HEAD did not stay behind the advanced primary tip.'

	# Healing isolation: a peer claim on a plan that exists only at the primary tip survives a behind-session claim-next.
	$peer = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$primary,'--branch','main','--owner','owner-peer','--session','session-peer','--write-claim-receipt',(Join-Path $primary 'Temp/peer.json'),'--plan','Documents/Plans/Test/PrimaryOnly.md')
	Assert-Result $peer 'claim-next' 'ok' 'claimed'; Assert-True ($peer.plan -ceq 'Documents/Plans/Test/PrimaryOnly.md') 'Peer did not claim the primary-only plan.'
	# Default behind-session selection ignores SessionKept (oldest of all, but dropped at the primary tip) and never heals the peer claim.
	$behindDefault = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-behind2','--session','session-behind2','--write-claim-receipt',(Join-Path $session 'Temp/behind-default.json'))
	Assert-Result $behindDefault 'claim-next' 'ok' 'claimed'; Assert-True ($behindDefault.claimed -and $behindDefault.plan -cne 'Documents/Plans/Test/SessionKept.md') 'Default selection claimed a path the primary tip had dropped.'
	$peerStatus = Invoke-Cli 0 @('plan','claim-status','--worktree',$primary,'--claim-receipt',$peer.receipt.path,'--claim-receipt-sha256',$peer.receipt.sha256)
	Assert-Result $peerStatus 'claim-status' 'ok' 'claimed'; Assert-True ($peerStatus.ownedByReceipt) 'Behind-session claim-next healed a peer claim on a primary-only plan.'
	# Primary-removed ineligibility: a targeted claim of a session-only path finds nothing claimable.
	$removedTarget = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-removed','--session','session-removed','--write-claim-receipt',(Join-Path $session 'Temp/removed.json'),'--plan','Documents/Plans/Test/SessionKept.md')
	Assert-Result $removedTarget 'claim-next' 'ok' 'none-available'; Assert-True (-not $removedTarget.claimed) 'A path removed at the primary tip was claimable.'
	# Dependency drift: the prerequisite lingers in the session tree, so the child stays blocked though the primary tip dropped it.
	$driftChild = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-drift','--session','session-drift','--write-claim-receipt',(Join-Path $session 'Temp/drift.json'),'--plan','Documents/Plans/Test/DriftChild.md')
	Assert-Result $driftChild 'claim-next' 'ok' 'none-available'; Assert-True (-not $driftChild.claimed) 'Child claimable while its session-tree prerequisite still exists.'
	Invoke-Cli 0 @('plan','unclaim','--worktree',$primary,'--claim-receipt',$peer.receipt.path,'--claim-receipt-sha256',$peer.receipt.sha256) | Out-Null
	Invoke-Cli 0 @('plan','unclaim','--worktree',$session,'--claim-receipt',$behindDefault.receipt.path,'--claim-receipt-sha256',$behindDefault.receipt.sha256) | Out-Null

	# One claim per session is independent of Plan-map membership: an awaiting-landing session whose own HEAD tree no longer
	# carries its plan, while the primary tip still does, must get that same terminal claim back rather than a second claim
	# on a still-eligible decoy plan. Claim and prepare-complete the plan, commit its removal, then advance the primary tip
	# past the removal and reintroduce the plan so the behind session HEAD lacks a plan the primary map still contains.
	& git.exe -C $session reset --hard main | Out-Null
	Set-Plan $primary 'Documents/Plans/Test/BehindOwned.md' '2019-01-01T00:00:00.000Z'
	Set-Plan $primary 'Documents/Plans/Test/BehindDecoy.md' '2019-01-02T00:00:00.000Z'
	Commit $primary 'owned terminal plan and decoy before behind divergence'
	& git.exe -C $session merge --ff-only main | Out-Null
	$behindOwnedBaseline = (& git.exe -C $session rev-parse HEAD).Trim()
	$behindOwnedClaim = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-behindowned','--session','session-behindowned','--write-claim-receipt',(Join-Path $session 'Temp/behind-owned.json'),'--plan','Documents/Plans/Test/BehindOwned.md')
	Assert-Result $behindOwnedClaim 'claim-next' 'ok' 'claimed'; Assert-True ($behindOwnedClaim.plan -ceq 'Documents/Plans/Test/BehindOwned.md' -and $behindOwnedClaim.baseline -ceq $behindOwnedBaseline) 'Owned terminal fixture did not claim its plan at the session baseline.'
	$behindOwnedPrepared = Invoke-Cli 0 @('plan','prepare-completion','--repo',$repo,'--worktree',$session,'--claim-receipt',$behindOwnedClaim.receipt.path,'--claim-receipt-sha256',$behindOwnedClaim.receipt.sha256)
	Assert-Result $behindOwnedPrepared 'prepare-completion' 'ok' 'prepared'; Assert-True ($behindOwnedPrepared.claimState -ceq 'awaiting-landing') 'Owned terminal fixture did not reach awaiting-landing.'
	Commit $session 'awaiting-landing session removes its own plan file'
	$behindOwnedHead = (& git.exe -C $session rev-parse HEAD).Trim()
	& git.exe -C $primary merge --ff-only $behindOwnedHead | Out-Null
	Set-Plan $primary 'Documents/Plans/Test/BehindOwned.md' '2019-01-01T00:00:00.000Z'; Commit $primary 'primary tip reintroduces the plan the behind session dropped'
	Assert-True ($behindOwnedHead -cne (& git.exe -C $primary rev-parse HEAD).Trim()) 'Behind session HEAD did not stay behind the advanced primary tip.'
	# Claim records are written hidden, so counting them requires -Force.
	$behindOwnedClaimsBefore = @(Get-ChildItem -LiteralPath $claimsDirectory -File -Force -Filter '*.json').Count
	$behindOwnedRetry = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-behindowned','--session','session-behindowned','--write-claim-receipt',(Join-Path $session 'Temp/behind-owned-retry.json'))
	Assert-Result $behindOwnedRetry 'claim-next' 'ok' 'claimed'
	Assert-True ($behindOwnedRetry.plan -ceq 'Documents/Plans/Test/BehindOwned.md' -and $behindOwnedRetry.baseline -ceq $behindOwnedBaseline -and $behindOwnedRetry.message -ceq 'existing terminal session claim returned') 'Behind session did not receive its map-independent terminal claim back.'
	$behindOwnedClaimsAfter = @(Get-ChildItem -LiteralPath $claimsDirectory -File -Force -Filter '*.json').Count
	Assert-True ($behindOwnedClaimsAfter -eq $behindOwnedClaimsBefore -and $behindOwnedClaimsAfter -eq 1) 'A second claim was minted for a session that already owned a terminal claim.'

	# Directory guidance carrying a valid byte-zero marker is committed to both trees with the oldest createdUtc of any
	# fixture Plan, so selection would reach it first if it were ever executable. A targeted claim must still find nothing.
	Set-Plan $primary $markedGuidance '2018-01-01T00:00:00.000Z'
	Commit $primary 'marked directory guidance at the primary tip'
	& git.exe -C $session merge --ff-only main | Out-Null
	$markedGuidanceClaim = Invoke-Cli 0 @('plan','claim-next','--repo',$repo,'--primary-worktree',$primary,'--worktree',$session,'--branch','fixture-session','--owner','owner-guidance','--session','session-guidance','--write-claim-receipt',(Join-Path $session 'Temp/marked-guidance.json'),'--plan',$markedGuidance)
	Assert-Result $markedGuidanceClaim 'claim-next' 'ok' 'none-available'; Assert-True (-not $markedGuidanceClaim.claimed) 'Directory guidance carrying a valid marker was claimable.'

	[pscustomobject]@{ schemaVersion = 'broken-engine-plan-scheduler-fixtures/v1'; status = 'pass'; code = 'ok'; cases = @('mandatory byte-zero metadata, directory-guidance exemption, canonical timestamps, and immutability','deep tracked paths','baseline deletion, primary-advance notice, and stale non-executable dependency classification','targeted validation','deterministic ordering','dependency and cycle quarantine','marker-less plan document loudness, unclaimability, and unrelated claimability','directory guidance non-executable and unclaimable with or without a marker','behind-session claim tolerance and diverged-session refusal','claim baseline identity, ancestry healing, and canonical timestamps','receipt-bound claims and unclaim','receipt containment and rollback','last-Plan missing-parent recovery and non-directory failure','terminal atomic orphan cleanup, awaiting retry, preparing recovery, and current-primary release proof','reconciled child body tolerance, restored dependency-edge rewrite, and named target third-party conflict','reparent across live claim states with receipt rewrite and post-rebase heal safety','no-claims and foreign-worktree reparent isolation','completion and release against landed squashed history','mid-conflict reparent with validate heal-delete hazard','reparent of a claim stamped at a post-advance primary commit newer than the receipt baseline','healing isolation, primary-removed ineligibility, and dependency drift','map-independent one-claim-per-session terminal reclaim') } | ConvertTo-Json -Depth 5 -Compress
}
finally { if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Force -Recurse } }

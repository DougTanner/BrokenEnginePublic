[CmdletBinding()]
param([Parameter(Mandatory)][string] $WorktreeCliExecutable)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$utf8 = [Text.UTF8Encoding]::new($false, $true)

function Assert-True([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Set-Plan([string] $Root, [string] $Path, [string] $CreatedUtc, [string[]] $DependsOn = @()) {
	$diskPath = Join-Path $Root $Path
	[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($diskPath)) | Out-Null
	if (Test-Path -LiteralPath $diskPath) { [IO.File]::SetAttributes($diskPath, [IO.FileAttributes]::Normal) }
	$metadata = [ordered]@{ createdUtc = $CreatedUtc; dependsOn = @($DependsOn) } | ConvertTo-Json -Depth 3 -Compress
	[IO.File]::WriteAllText($diskPath, "<!-- broken-engine-plan/v1 $metadata -->`n# $([IO.Path]::GetFileNameWithoutExtension($Path))`n", $utf8)
}
function Commit([string] $Worktree, [string] $Message) { & git.exe -C $Worktree add --all -- Documents | Out-Null; & git.exe -C $Worktree commit -m $Message | Out-Null }

function Invoke-Cli([int] $ExpectedExit, [string[]] $Arguments) {
	$output = @(& $WorktreeCliExecutable @Arguments 2>&1)
	$exit = $LASTEXITCODE
	Assert-True ($exit -eq $ExpectedExit) "WorktreeCli exited ${exit}, expected ${ExpectedExit}: $($Arguments -join ' '); output=$($output -join "`n")"
	try { $json = ($output -join "`n" | ConvertFrom-Json -Depth 100 -ErrorAction Stop) } catch { throw "WorktreeCli did not return one JSON value: $($output -join "`n")" }
	foreach ($field in @('schemaVersion', 'status', 'code')) { Assert-True ($json.PSObject.Properties.Name -ccontains $field) "Scheduler response omits '$field': $($Arguments -join ' ')." }
	return $json
}
function Assert-Code($Result, [string] $Status, [string] $Code) {
	Assert-True ($Result.status -ceq $Status -and $Result.code -ceq $Code) "Unexpected result: $($Result | ConvertTo-Json -Depth 20 -Compress)"
}
# Claim-surface responses carry the v2 identity envelope; `plan validate` keeps the pre-existing lint envelope.
function Assert-ClaimEnvelope($Result, [string] $Plan, [string] $Owner, [string] $Session, [string] $Worktree, [string] $Branch) {
	Assert-True ($Result.schemaVersion -eq 2) 'Claim response schemaVersion is not 2.'
	foreach ($field in @('plan', 'owner', 'session', 'worktree', 'branch', 'claimedAt', 'expiresAt')) {
		Assert-True ($Result.PSObject.Properties.Name -ccontains $field) "Claim response omits '$field'."
	}
	Assert-True ($Result.plan -ceq $Plan -and $Result.owner -ceq $Owner -and $Result.session -ceq $Session -and $Result.branch -ceq $Branch) "Claim response identity does not match the request: $($Result | ConvertTo-Json -Depth 20 -Compress)"
	Assert-True ((Get-Item -LiteralPath $Result.worktree).FullName.TrimEnd('\') -ieq (Get-Item -LiteralPath $Worktree).FullName.TrimEnd('\')) 'Claim response worktree is not the requesting worktree.'
	$claimedAt = [DateTimeOffset]::Parse($Result.claimedAt, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind)
	$expiresAt = [DateTimeOffset]::Parse($Result.expiresAt, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind)
	Assert-True (($expiresAt - $claimedAt).TotalHours -eq 48) 'Claim lease is not the 48-hour expiry window.'
}

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

	function Invoke-ClaimNext([int] $Exit, [string] $Worktree, [string] $Branch, [string] $Owner, [string] $Session, [string] $Plan, [string[]] $Extra = @()) {
		$arguments = @('plan', 'claim-next', '--repo', $repo, '--primary-worktree', $primary, '--worktree', $Worktree, '--branch', $Branch, '--owner', $Owner, '--session', $Session)
		if (-not [string]::IsNullOrWhiteSpace($Plan)) { $arguments += @('--plan', $Plan) }
		return Invoke-Cli $Exit ($arguments + $Extra)
	}
	function Invoke-ClaimOperation([int] $Exit, [string] $Command, [string] $Worktree, [string] $Owner, [string] $Session, [string[]] $Extra = @()) {
		return Invoke-Cli $Exit (@('plan', $Command, '--repo', $repo, '--worktree', $Worktree, '--owner', $Owner, '--session', $Session) + $Extra)
	}
	# Every path and byte under the isolated scheduler state root, so a read-only verb is proved to add, delete, or
	# rewrite nothing rather than only leaving the claim records it happened to read alone.
	function Get-SchedulerState() {
		return (@(Get-ChildItem -LiteralPath $localAppData -Force -Recurse | Sort-Object FullName | ForEach-Object {
			if ($_.PSIsContainer) { $_.FullName } else { $_.FullName + '=' + [Convert]::ToBase64String([IO.File]::ReadAllBytes($_.FullName)) }
		}) -join "`n")
	}
	function Get-ClaimRecords() {
		$claims = Get-ChildItem -LiteralPath $localAppData -Directory -Recurse | Where-Object { $_.Name -ceq 'claims' } | Select-Object -First 1 -ExpandProperty FullName
		Assert-True (-not [string]::IsNullOrWhiteSpace($claims)) 'Scheduler did not create isolated claim storage.'
		return @(Get-ChildItem -LiteralPath $claims -File -Force -Filter '*.json')
	}

	# Equal createdUtc across Alpha/Zulu proves the (createdUtc, path) tiebreak; Dependent is blocked until
	# Older completes; Self/CycleA/CycleB quarantine their own component only.
	Set-Plan $primary 'Documents/Plans/Test/Older.md' '2024-01-01T00:00:00.000Z'
	Set-Plan $primary 'Documents/Plans/Test/Alpha.md' '2024-01-02T00:00:00.000Z'
	Set-Plan $primary 'Documents/Plans/Test/Zulu.md' '2024-01-02T00:00:00.000Z'
	Set-Plan $primary 'Documents/Plans/Test/Dependent.md' '2024-01-04T00:00:00.000Z' @('Documents/Plans/Test/Older.md')
	Set-Plan $primary 'Documents/Plans/Test/Transient.md' '2024-01-05T00:00:00.000Z'
	Set-Plan $primary 'Documents/Plans/Test/LateBlocked.md' '2024-01-06T00:00:00.000Z'
	Set-Plan $primary 'Documents/Plans/Test/CycleA.md' '2024-01-07T00:00:00.000Z' @('Documents/Plans/Test/CycleB.md')
	Set-Plan $primary 'Documents/Plans/Test/CycleB.md' '2024-01-08T00:00:00.000Z' @('Documents/Plans/Test/CycleA.md')
	[IO.File]::WriteAllText((Join-Path $primary 'Documents/Plans/Test/AGENTS.md'), '# Directory guidance', $utf8)
	Commit $primary 'baseline'
	$baseline = (& git.exe -C $primary rev-parse HEAD).Trim()
	& git.exe -C $primary worktree add -b fixture-session $session $baseline | Out-Null

	# Lint surface: the cycle component is quarantined by name, directory guidance is never executable, and a
	# marker-less plan document is loud on its own path without blocking anything else.
	$markerless = 'Documents/Plans/Test/Markerless.md'
	[IO.File]::WriteAllText((Join-Path $primary $markerless), '# Marker-less plan document', $utf8)
	& git.exe -C $primary add --all -- Documents/Plans/Test | Out-Null
	$validate = Invoke-Cli 0 @('plan', 'validate', '--repo', $repo, '--worktree', $primary)
	Assert-Code $validate 'invalid' 'invalid-plans'
	Assert-True (@($validate.diagnostics | Where-Object code -ceq 'dependency-cycle').Count -eq 2) 'Cycle component was not quarantined.'
	Assert-True (@($validate.diagnostics | Where-Object { $_.plan -ceq $markerless -and $_.code -ceq 'invalid-metadata' }).Count -eq 1 -and @($validate.plans | Where-Object { $_.path -ceq $markerless }).Count -eq 0) 'Marker-less plan document was not reported once as non-executable.'
	Assert-True (@($validate.plans | Where-Object { $_.path -clike '*AGENTS.md' }).Count -eq 0) 'Directory guidance became an executable Plan.'
	Assert-True ($validate.plans[0].path -ceq 'Documents/Plans/Test/Older.md') 'Validation did not retain deterministic inventory order.'
	Remove-Item -LiteralPath (Join-Path $primary $markerless) -Force; & git.exe -C $primary reset -- $markerless | Out-Null

	# `plan list` reads committed trees, so the marker-less document is committed for one listing pass and removed
	# again: the listing keeps the validated inventory order, reports that document as a diagnostic instead of a row,
	# and shows the dependency child as blocked by the prerequisite that is still present.
	[IO.File]::WriteAllText((Join-Path $primary $markerless), '# Marker-less plan document', $utf8)
	Commit $primary 'commit the marker-less document'
	$listing = Invoke-Cli 0 @('plan', 'list', '--repo', $repo, '--worktree', $primary)
	Assert-Code $listing 'ok' 'ok'; Assert-True ($listing.operation -ceq 'list') 'Listing did not report the list operation.'
	# Validation drops a quarantined plan from its inventory while the listing keeps it as a row, so the shared order is
	# compared over the rows validation also reports.
	Assert-True ((@($listing.plans | Where-Object { $_.state -cne 'quarantined' } | ForEach-Object { $_.path }) -join '|') -ceq (@($validate.plans | ForEach-Object { $_.path }) -join '|')) 'Listing did not reuse the validated inventory order.'
	Assert-True ((@($listing.plans | Where-Object { $_.state -ceq 'eligible' } | ForEach-Object { $_.path }) -join '|') -ceq 'Documents/Plans/Test/Older.md|Documents/Plans/Test/Alpha.md|Documents/Plans/Test/Zulu.md|Documents/Plans/Test/Transient.md|Documents/Plans/Test/LateBlocked.md') 'Listing did not report the eligible Plans in selection order.'
	Assert-True (@($listing.plans | Where-Object { $_.path -ceq $markerless }).Count -eq 0 -and @($listing.diagnostics | Where-Object { $_.plan -ceq $markerless -and $_.code -ceq 'invalid-metadata' }).Count -eq 1) 'Listing did not report the committed marker-less document as a diagnostic instead of a row.'
	$dependentRow = @($listing.plans | Where-Object { $_.path -ceq 'Documents/Plans/Test/Dependent.md' })[0]
	Assert-True ($dependentRow.state -ceq 'blocked' -and ($dependentRow.blockedBy -ccontains 'Documents/Plans/Test/Older.md')) 'Listing did not name the unmet prerequisite of the blocked Plan.'
	Remove-Item -LiteralPath (Join-Path $primary $markerless) -Force; Commit $primary 'remove the marker-less document'

	# The cutover transition shim is gone: a retired option is an unknown option again and fails as a usage error.
	$retiredOption = Invoke-Cli 1 @('plan', 'validate', '--repo', $repo, '--worktree', $primary, '--baseline', $baseline)
	Assert-Code $retiredOption 'error' 'usage'
	# --plan scopes only the reported inventory; diagnostics stay whole-repo, so the committed cycle keeps the
	# envelope invalid while the targeted inventory still carries exactly the requested Plan.
	$targeted = Invoke-Cli 0 @('plan', 'validate', '--repo', $repo, '--worktree', $primary, '--plan', 'Documents/Plans/Test/Older.md')
	Assert-Code $targeted 'invalid' 'invalid-plans'; Assert-True (@($targeted.plans).Count -eq 1 -and $targeted.plans[0].path -ceq 'Documents/Plans/Test/Older.md') 'Targeted validation did not select only the requested Plan.'

	# Claim lifecycle: deterministic selection, idempotent re-claim, one claim per session, foreign refusal.
	$noClaim = Invoke-ClaimOperation 0 'claim-status' $session 'owner-a' 'session-a'
	Assert-Code $noClaim 'ok' 'none'
	$claim = Invoke-ClaimNext 0 $session 'fixture-session' 'owner-a' 'session-a' $null
	Assert-Code $claim 'ok' 'claimed'; Assert-ClaimEnvelope $claim 'Documents/Plans/Test/Older.md' 'owner-a' 'session-a' $session 'fixture-session'
	$reclaim = Invoke-ClaimNext 0 $session 'fixture-session' 'owner-a' 'session-a' 'Documents/Plans/Test/Older.md'
	Assert-Code $reclaim 'ok' 'existing'; Assert-True ($reclaim.plan -ceq $claim.plan -and $reclaim.claimedAt -ceq $claim.claimedAt) 'Idempotent re-claim minted a new lease.'
	$secondPlan = Invoke-ClaimNext 0 $session 'fixture-session' 'owner-a' 'session-a' 'Documents/Plans/Test/Alpha.md'
	Assert-Code $secondPlan 'ok' 'existing'; Assert-True ($secondPlan.plan -ceq 'Documents/Plans/Test/Older.md') 'A session holding a claim was given a second one.'
	Assert-True (@(Get-ClaimRecords).Count -eq 1) 'A second claim record was minted for one session.'
	$status = Invoke-ClaimOperation 0 'claim-status' $session 'owner-a' 'session-a'
	Assert-True ($status.status -ceq 'ok' -and $status.code -cin @('claimed', 'existing')) "claim-status did not report the live claim: $($status | ConvertTo-Json -Depth 20 -Compress)"
	Assert-True ($status.plan -ceq 'Documents/Plans/Test/Older.md') 'claim-status reported the wrong plan.'
	$foreign = Invoke-ClaimNext 0 $primary 'main' 'owner-b' 'session-b' 'Documents/Plans/Test/Older.md'
	Assert-Code $foreign 'ok' 'none'
	# Dependent stays blocked while its prerequisite exists, so the tiebreak winner is Alpha, not Zulu.
	$tiebreak = Invoke-ClaimNext 0 $primary 'main' 'owner-b' 'session-b' $null
	Assert-Code $tiebreak 'ok' 'claimed'; Assert-True ($tiebreak.plan -ceq 'Documents/Plans/Test/Alpha.md') 'Equal-createdUtc selection did not break the tie by UTF-8 path.'
	$released = Invoke-ClaimOperation 0 'unclaim' $primary 'owner-b' 'session-b'
	Assert-Code $released 'ok' 'released'
	$absent = Invoke-ClaimOperation 0 'unclaim' $primary 'owner-b' 'session-b'
	Assert-Code $absent 'ok' 'already-absent'
	$reclaimable = Invoke-ClaimNext 0 $primary 'main' 'owner-b2' 'session-b2' 'Documents/Plans/Test/Alpha.md'
	Assert-Code $reclaimable 'ok' 'claimed'; Invoke-ClaimOperation 0 'unclaim' $primary 'owner-b2' 'session-b2' | Out-Null

	# A cycle-quarantined Plan is never claimable.
	$cycleTarget = Invoke-ClaimNext 0 $primary 'main' 'owner-cycle' 'session-cycle' 'Documents/Plans/Test/CycleA.md'
	Assert-Code $cycleTarget 'ok' 'none'

	# Expiry healing: an expired lease is removed before candidates are evaluated, so a peer claims the plan.
	$expiring = Invoke-ClaimNext 0 $primary 'main' 'owner-expire' 'session-expire' 'Documents/Plans/Test/Zulu.md'
	Assert-Code $expiring 'ok' 'claimed'
	$expiringRecord = @(Get-ClaimRecords | Where-Object { (Get-Content -LiteralPath $_.FullName -Raw) -match 'Zulu\.md' })
	Assert-True ($expiringRecord.Count -eq 1) 'Expiring claim did not produce exactly one record.'
	$record = Get-Content -LiteralPath $expiringRecord[0].FullName -Raw | ConvertFrom-Json -Depth 100
	$record.claimedAt = '2000-01-01T00:00:00.0000000Z'; $record.expiresAt = '2000-01-03T00:00:00.0000000Z'
	[IO.File]::SetAttributes($expiringRecord[0].FullName, [IO.FileAttributes]::Normal)
	[IO.File]::WriteAllText($expiringRecord[0].FullName, (($record | ConvertTo-Json -Depth 100) + "`n"), $utf8)
	$afterExpiry = Invoke-ClaimNext 0 $primary 'main' 'owner-heir' 'session-heir' 'Documents/Plans/Test/Zulu.md'
	Assert-Code $afterExpiry 'ok' 'claimed'; Assert-True ($afterExpiry.session -ceq 'session-heir') 'Expired lease was not healed for the new owner.'
	Invoke-ClaimOperation 0 'unclaim' $primary 'owner-heir' 'session-heir' | Out-Null

	# Schema healing: any record the current schema cannot read - including every v1 record - is garbage
	# collected before candidate evaluation rather than blocking a claim.
	$claimsDirectory = (Get-ChildItem -LiteralPath $localAppData -Directory -Recurse | Where-Object { $_.Name -ceq 'claims' } | Select-Object -First 1).FullName
	$legacy = Join-Path $claimsDirectory 'legacy-v1.json'
	[IO.File]::WriteAllText($legacy, ([ordered]@{ schemaVersion = 1; plan = 'Documents/Plans/Test/Zulu.md'; owner = 'owner-v1'; session = 'session-v1'; state = 'claimed' } | ConvertTo-Json -Depth 5), $utf8)
	$corrupt = Join-Path $claimsDirectory 'corrupt.json'; [IO.File]::WriteAllText($corrupt, '{not-json', $utf8)
	$afterHeal = Invoke-ClaimNext 0 $primary 'main' 'owner-heal' 'session-heal' 'Documents/Plans/Test/Zulu.md'
	Assert-Code $afterHeal 'ok' 'claimed'
	Assert-True ((-not (Test-Path -LiteralPath $legacy)) -and (-not (Test-Path -LiteralPath $corrupt))) 'v1-schema or unreadable claim records were not heal-deleted.'
	Invoke-ClaimOperation 0 'unclaim' $primary 'owner-heal' 'session-heal' | Out-Null

	# Absent-at-primary healing keeps best-effort post-landing unclaim safe: a claim whose Plan the primary
	# tip no longer carries is healed, and the owning session is free to claim again.
	$transient = Invoke-ClaimNext 0 $session 'fixture-session' 'owner-transient' 'session-transient' 'Documents/Plans/Test/Transient.md'
	Assert-Code $transient 'ok' 'claimed'
	# A foreign live claim is visible on its own row, and the listing that reads it leaves every scheduler byte alone.
	$stateBeforeListing = Get-SchedulerState
	$claimedListing = Invoke-Cli 0 @('plan', 'list', '--repo', $repo, '--worktree', $primary)
	$claimedRow = @($claimedListing.plans | Where-Object { $_.path -ceq 'Documents/Plans/Test/Transient.md' })[0]
	Assert-True ($claimedRow.state -ceq 'claimed' -and ($claimedRow.PSObject.Properties.Name -ccontains 'claim')) 'Listing did not report the live claim on its plan row.'
	Assert-True ($claimedRow.claim.session -ceq 'session-transient' -and -not [string]::IsNullOrWhiteSpace($claimedRow.claim.expiresAt)) 'Listing did not report the live claim identity and expiry.'
	Assert-True ((Get-SchedulerState) -ceq $stateBeforeListing) 'Listing changed machine-local scheduler state.'
	Remove-Item -LiteralPath (Join-Path $primary 'Documents/Plans/Test/Transient.md') -Force
	Commit $primary 'peer landing removes the transient plan'
	# Healing runs in `plan validate` and `plan claim-next`; `claim-status` is a pure read that never heals.
	Invoke-Cli 0 @('plan', 'validate', '--repo', $repo, '--worktree', $primary) | Out-Null
	$afterAbsent = Invoke-ClaimOperation 0 'claim-status' $session 'owner-transient' 'session-transient'
	Assert-Code $afterAbsent 'ok' 'none'

	# A genuinely diverged session (a local commit absent from the primary tip) is a hard error.
	& git.exe -C $session merge --ff-only main | Out-Null
	Set-Plan $session 'Documents/Plans/Test/Diverged.md' '2024-01-09T00:00:00.000Z'
	Commit $session 'diverged local work'
	$diverged = Invoke-ClaimNext 1 $session 'fixture-session' 'owner-diverged' 'session-diverged' $null
	Assert-True ($diverged.status -ceq 'error') 'Diverged session claim did not fail as an error.'
	& git.exe -C $session reset --hard main | Out-Null

	# A dependency edge landed at the primary tip after this session's baseline blocks the claim even though the
	# behind session's own tree still carries the edge-free marker.
	Set-Plan $primary 'Documents/Plans/Test/LateBlocked.md' '2024-01-06T00:00:00.000Z' @('Documents/Plans/Test/Older.md')
	Commit $primary 'peer landing adds a dependency edge'
	Assert-True (-not ([IO.File]::ReadAllText((Join-Path $session 'Documents/Plans/Test/LateBlocked.md')) -match 'Older\.md')) 'Session tree already carries the primary-only dependency edge.'
	$lateBlocked = Invoke-ClaimNext 0 $session 'fixture-session' 'owner-late' 'session-late' 'Documents/Plans/Test/LateBlocked.md'
	Assert-Code $lateBlocked 'ok' 'none'

	# Rejection requires explicit user authorization; the authorized form deletes the target like completion.
	$reject = Invoke-ClaimNext 0 $session 'fixture-session' 'owner-reject' 'session-reject' 'Documents/Plans/Test/Alpha.md'
	Assert-Code $reject 'ok' 'claimed'
	$unauthorized = Invoke-ClaimOperation 1 'reject' $session 'owner-reject' 'session-reject'
	Assert-True ($unauthorized.status -ceq 'error') 'Rejection without --user-authorized-rejection was accepted.'
	Assert-True (Test-Path -LiteralPath (Join-Path $session 'Documents/Plans/Test/Alpha.md')) 'Unauthorized rejection deleted the target Plan.'
	$rejected = Invoke-ClaimOperation 0 'reject' $session 'owner-reject' 'session-reject' @('--user-authorized-rejection')
	Assert-Code $rejected 'ok' 'rejected'
	Assert-True ($rejected.plan -ceq 'Documents/Plans/Test/Alpha.md' -and $rejected.changedPaths -contains 'Documents/Plans/Test/Alpha.md') 'Rejection did not report its changed paths.'
	Assert-True (-not (Test-Path -LiteralPath (Join-Path $session 'Documents/Plans/Test/Alpha.md'))) 'Rejection did not delete the target Plan.'
	Invoke-ClaimOperation 0 'unclaim' $session 'owner-reject' 'session-reject' | Out-Null
	& git.exe -C $session checkout -- Documents/Plans/Test/Alpha.md | Out-Null

	# A terminal operation invoked from a checkout other than the claim's own worktree is a state conflict: the
	# claim is found by owner/session identity, so an unguarded run would mutate the wrong tree's bytes.
	$primaryTarget = Join-Path $primary 'Documents/Plans/Test/Older.md'
	$primaryChild = Join-Path $primary 'Documents/Plans/Test/Dependent.md'
	$mismatch = Invoke-ClaimOperation 2 'complete' $primary 'owner-a' 'session-a'
	Assert-Code $mismatch 'error' 'claim-worktree-mismatch'
	Assert-True ((Test-Path -LiteralPath $primaryTarget) -and ([IO.File]::ReadAllText($primaryChild) -match 'Older\.md')) 'Mismatched-worktree completion mutated the wrong checkout.'
	Assert-True ((Test-Path -LiteralPath (Join-Path $session 'Documents/Plans/Test/Older.md')) -and ([IO.File]::ReadAllText((Join-Path $session 'Documents/Plans/Test/Dependent.md')) -match 'Older\.md')) 'Mismatched-worktree completion mutated the claim worktree.'
	$heldAfterMismatch = Invoke-ClaimOperation 0 'claim-status' $session 'owner-a' 'session-a'
	Assert-True ($heldAfterMismatch.code -cin @('claimed', 'existing') -and $heldAfterMismatch.plan -ceq 'Documents/Plans/Test/Older.md') 'Mismatched-worktree completion disturbed the claim record.'

	# Completion rewrites direct dependency children, deletes only the target, and reruns idempotently. The
	# claim survives completion; it is released at landing.
	$childPath = Join-Path $session 'Documents/Plans/Test/Dependent.md'
	$complete = Invoke-ClaimOperation 0 'complete' $session 'owner-a' 'session-a'
	Assert-Code $complete 'ok' 'completed'
	Assert-True ($complete.plan -ceq 'Documents/Plans/Test/Older.md') 'Completion reported the wrong plan.'
	Assert-True (($complete.changedPaths -contains 'Documents/Plans/Test/Older.md') -and ($complete.changedPaths -contains 'Documents/Plans/Test/Dependent.md')) 'Completion did not report target deletion and direct child rewrite.'
	Assert-True (-not (Test-Path -LiteralPath (Join-Path $session 'Documents/Plans/Test/Older.md'))) 'Completion did not delete the target Plan.'
	Assert-True (-not ([IO.File]::ReadAllText($childPath) -match 'Older\.md')) 'Completion did not rewrite the direct child marker.'
	$completeRetry = Invoke-ClaimOperation 0 'complete' $session 'owner-a' 'session-a'
	Assert-Code $completeRetry 'ok' 'completed'; Assert-True (@($completeRetry.changedPaths).Count -eq 0) 'Completion rerun was not idempotent.'
	$heldAfterCompletion = Invoke-ClaimOperation 0 'claim-status' $session 'owner-a' 'session-a'
	Assert-True ($heldAfterCompletion.code -cin @('claimed', 'existing')) 'Completion released the claim before landing.'

	[pscustomobject]@{ schemaVersion = 'broken-engine-plan-scheduler-fixtures/v1'; status = 'pass'; code = 'ok'; cases = @(
		'lint envelope, cycle quarantine, marker-less loudness, and directory-guidance exemption',
		'retired-option usage error and targeted validation',
		'listing inventory order, eligible selection order, blocked prerequisites, and marker-less diagnostic',
		'listing reports a live claim and leaves every scheduler byte unchanged',
		'claim lease envelope, 48-hour expiry window, and deterministic (createdUtc, path) selection',
		'idempotent re-claim and one claim per session',
		'foreign live-claim refusal and unclaim released/already-absent',
		'cycle-quarantined Plans are unclaimable',
		'expired lease healing',
		'v1-schema and unreadable claim record heal-deletion',
		'absent-at-primary healing and diverged-session refusal',
		'primary-tip dependency edge landed after the session baseline blocks the claim',
		'rejection authorization gate and target deletion',
		'terminal operation from a non-claim worktree is a state conflict that mutates nothing',
		'completion child rewrite, target deletion, idempotent rerun, and claim retention') } | ConvertTo-Json -Depth 5 -Compress
}
finally {
	[Environment]::SetEnvironmentVariable('LOCALAPPDATA', $originalLocalAppData)
	if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Force -Recurse }
}

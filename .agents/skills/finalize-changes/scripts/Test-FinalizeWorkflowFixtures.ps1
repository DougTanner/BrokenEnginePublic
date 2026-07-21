# Scratch-repository coverage for finalization preflight request binding, one
# session landing, and idempotent post-advance recovery. Uses the supplied
# capability-current WorktreeCli only inside disposable Output and never touches
# a real primary checkout, queue, or canonical executable.
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $WorktreeCliExecutable
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\WorktreeCliSessionExclusion.psm1') -Force -DisableNameChecking

$script:Failures = [Collections.Generic.List[string]]::new()
$preflightScript = Join-Path $PSScriptRoot 'Test-FinalizePreflight.ps1'
$landingScript = Join-Path $PSScriptRoot 'Invoke-FinalizeLanding.ps1'
$moduleSource = Join-Path $PSScriptRoot '..\..\..\scripts'
$WorktreeCliExecutable = (Get-Item -LiteralPath $WorktreeCliExecutable -Force -ErrorAction Stop).FullName

function Assert-True([bool] $Condition, [string] $Name) {
	if ($Condition) { Write-Host "pass $Name" } else { $script:Failures.Add($Name); Write-Host "FAIL $Name" }
}

function Assert-SafeScratchRoot([string] $Parent, [string] $Root, [string] $ExpectedLeaf) {
	$parentPath = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
	$rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
	if ((Split-Path -Parent $rootPath) -cne $parentPath -or (Split-Path -Leaf $rootPath) -cne $ExpectedLeaf -or $ExpectedLeaf -cnotmatch '^[0-9a-f]{32}$') {
		throw "Fixture scratch root failed containment validation: '$rootPath'."
	}
	return $rootPath
}

function Invoke-ScratchGit([string] $Root, [string[]] $Arguments) {
	$output = @(& git -C $Root -c user.name=fixture -c user.email=fixture@example.com @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $($output -join '; ')" }
	return $output
}

function Invoke-JsonScript([string] $Script, [string[]] $Arguments) {
	$stdout = @(& "$PSHOME\pwsh.exe" -NoProfile -File $Script @Arguments 2>$null)
	$exitCode = $LASTEXITCODE
	$text = ($stdout -join "`n").Trim()
	$json = $null
	try { if (-not [string]::IsNullOrWhiteSpace($text)) { $json = $text | ConvertFrom-Json -Depth 100 -ErrorAction Stop } } catch { }
	return [pscustomobject]@{ ExitCode = $exitCode; Json = $json; Text = $text }
}

function Invoke-JsonScriptWithSplat([string] $Script, [Collections.IDictionary] $Parameters, [string] $ScratchRoot) {
	$invocationRoot = Join-Path $ScratchRoot 'splat-invocation'
	New-Item -ItemType Directory -Force $invocationRoot | Out-Null
	$payloadPath = Join-Path $invocationRoot ([guid]::NewGuid().ToString('N') + '.json')
	$wrapperPath = Join-Path $invocationRoot 'Invoke-WithSplat.ps1'
	$wrapper = @'
param([string] $TargetScript, [string] $PayloadPath)
$payload = Get-Content -LiteralPath $PayloadPath -Raw | ConvertFrom-Json -Depth 32 -ErrorAction Stop
$parameters = @{}
foreach ($property in $payload.PSObject.Properties) { $parameters[$property.Name] = $property.Value }
& $TargetScript @parameters
'@
	[IO.File]::WriteAllText($wrapperPath, $wrapper, [Text.UTF8Encoding]::new($false))
	[IO.File]::WriteAllText($payloadPath, (ConvertTo-Json -InputObject $Parameters -Depth 8 -Compress), [Text.UTF8Encoding]::new($false))
	$stdout = @(& "$PSHOME\pwsh.exe" -NoProfile -File $wrapperPath -TargetScript $Script -PayloadPath $payloadPath 2>$null)
	$exitCode = $LASTEXITCODE
	$text = ($stdout -join "`n").Trim()
	$json = $null
	try { if (-not [string]::IsNullOrWhiteSpace($text)) { $json = $text | ConvertFrom-Json -Depth 100 -ErrorAction Stop } } catch { }
	return [pscustomobject]@{ ExitCode = $exitCode; Json = $json; Text = $text }
}

function Invoke-WorktreeCli([string[]] $Arguments, [int] $ExpectedExitCode = 0) {
	$stdout = @(& $WorktreeCliExecutable @Arguments 2>&1)
	if ($LASTEXITCODE -ne $ExpectedExitCode) {
		throw "WorktreeCli $($Arguments -join ' ') exited $LASTEXITCODE, expected $ExpectedExitCode`: $($stdout -join '; ')"
	}
	return $stdout
}

function Assert-Outcome($Run, [string] $Case, [int] $ExpectedExit, [string] $ExpectedStatus, [string] $ExpectedCode) {
	Assert-True ($null -ne $Run.Json) "$Case emitted JSON"
	if ($null -eq $Run.Json) { Write-Host "  stdout: $($Run.Text)"; return }
	Assert-True ($Run.ExitCode -eq $ExpectedExit) "$Case exit=$ExpectedExit (was $($Run.ExitCode))"
	Assert-True ($Run.Json.status -ceq $ExpectedStatus) "$Case status=$ExpectedStatus (was $($Run.Json.status))"
	Assert-True ($Run.Json.code -ceq $ExpectedCode) "$Case code=$ExpectedCode (was $($Run.Json.code))"
	if ($Run.ExitCode -ne $ExpectedExit -or $Run.Json.code -cne $ExpectedCode) { Write-Host "  message: $($Run.Json.message)" }
}

function New-PlanRowStatusFixtureModule {
	$tokens = $null
	$errors = $null
	$ast = [Management.Automation.Language.Parser]::ParseFile($landingScript, [ref]$tokens, [ref]$errors)
	if ($errors.Count -ne 0) { throw "Landing script has parser errors: $($errors.Message -join '; ')" }
	$functionAst = $ast.Find({
		param($Node)
		$Node -is [Management.Automation.Language.FunctionDefinitionAst] -and $Node.Name -ceq 'Resolve-PlanRowStatus'
	}, $true)
	if ($null -eq $functionAst) { throw 'Landing script does not define Resolve-PlanRowStatus.' }
	return New-Module -ArgumentList $functionAst.Extent.Text -ScriptBlock {
		param([string] $FunctionDefinition)
		function Throw-Landing([int] $ExitCode, [string] $Code, [string] $Message) {
			$exception = [InvalidOperationException]::new($Message)
			$exception.Data['FinalizeExitCode'] = $ExitCode
			$exception.Data['FinalizeCode'] = $Code
			throw $exception
		}
		. ([scriptblock]::Create($FunctionDefinition))
	}
}

function Invoke-PlanRowStatusFixture($Module, [int] $ExitCode, $Status) {
	try {
		$state = & $Module { param($Code, $Value) Resolve-PlanRowStatus $Code $Value } $ExitCode $Status
		return [pscustomobject]@{ State = $state; Code = $null }
	}
	catch {
		return [pscustomobject]@{ State = $null; Code = [string]$_.Exception.Data['FinalizeCode'] }
	}
}

$planRowStatusModule = New-PlanRowStatusFixtureModule
$heldZero = Invoke-PlanRowStatusFixture $planRowStatusModule 2 ('{"held":0}' | ConvertFrom-Json)
Assert-True ($heldZero.Code -ceq 'plan-row.status-failed') 'row status held:0 fails closed'
$heldString = Invoke-PlanRowStatusFixture $planRowStatusModule 2 ('{"held":"false"}' | ConvertFrom-Json)
Assert-True ($heldString.Code -ceq 'plan-row.status-failed') 'row status held:"false" fails closed'
$ownedZero = Invoke-PlanRowStatusFixture $planRowStatusModule 0 ('{"ownedByRequester":0}' | ConvertFrom-Json)
Assert-True ($ownedZero.Code -ceq 'plan-row.status-failed') 'row status ownedByRequester:0 fails closed'
$ownedString = Invoke-PlanRowStatusFixture $planRowStatusModule 0 ('{"ownedByRequester":"false"}' | ConvertFrom-Json)
Assert-True ($ownedString.Code -ceq 'plan-row.status-failed') 'row status ownedByRequester:"false" fails closed'
$foreign = Invoke-PlanRowStatusFixture $planRowStatusModule 0 ('{"ownedByRequester":false}' | ConvertFrom-Json)
Assert-True ($foreign.Code -ceq 'plan-row.not-owned') 'canonical foreign row status remains not-owned'
$absent = Invoke-PlanRowStatusFixture $planRowStatusModule 2 ('{"held":false}' | ConvertFrom-Json)
Assert-True ($absent.State -ceq 'absent' -and $null -eq $absent.Code) 'canonical absent row status remains absent'
$owned = Invoke-PlanRowStatusFixture $planRowStatusModule 0 ('{"ownedByRequester":true}' | ConvertFrom-Json)
Assert-True ($owned.State -ceq 'owned' -and $null -eq $owned.Code) 'canonical owned row status remains owned'

$scratchParent = Join-Path ([IO.Path]::GetTempPath()) 'BrokenEngineFinalizeWorkflowFixtures'
$scratchLeaf = [guid]::NewGuid().ToString('N')
$scratchBase = Assert-SafeScratchRoot $scratchParent (Join-Path $scratchParent $scratchLeaf) $scratchLeaf
$primary = Join-Path $scratchBase 'primary'
$session = Join-Path $scratchBase 'session'
$localAppData = Join-Path $scratchBase 'local-app-data'
$previousEnvironment = @{}
$fixtureEnvironment = $null
$owner = $null
$ownerRegistered = $false
$fixtureExitCode = 0

try {
New-Item -ItemType Directory -Force $primary | Out-Null
New-Item -ItemType Directory -Force $localAppData | Out-Null
Invoke-ScratchGit $primary @('init', '-b', 'main') | Out-Null
New-Item -ItemType Directory -Force (Join-Path $primary '.agents\scripts') | Out-Null
foreach ($module in @('AgentScriptCommon.psm1', 'WorktreeCliSessionExclusion.psm1')) {
	Copy-Item -LiteralPath (Join-Path $moduleSource $module) -Destination (Join-Path $primary ".agents\scripts\$module") -Force
}
[IO.File]::WriteAllText((Join-Path $primary '.gitignore'), "Temp/`nTools/WorktreeCli/Platforms/VisualStudio2026/Output/`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $primary 'base.txt'), 'base', [Text.UTF8Encoding]::new($false))
New-Item -ItemType Directory -Force (Join-Path $primary 'Documents\Plans'), (Join-Path $primary 'Documents\Features') | Out-Null
$plansOrder = @(
	'# Plan Execution Order', '', '## Plans', '',
	'| Plan | Tier | Effort | Impact | Risks | Score | Depends On | Notes |',
	'|------|------|--------|--------|-------|-------|------------|-------|',
	'| [Recovery.md](Recovery.md) | Small | 2 | 3 | 1 | 0 | - | finalize recovery fixture |', ''
) -join "`n"
$featuresOrder = @(
	'# Feature Execution Order', '', '## Plans', '',
	'| Plan | Tier | Effort | Impact | Risks | Score | Depends On | Notes |',
	'|------|------|--------|--------|-------|-------|------------|-------|', ''
) -join "`n"
[IO.File]::WriteAllText((Join-Path $primary 'Documents\Plans\Order.md'), $plansOrder, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $primary 'Documents\Features\Order.md'), $featuresOrder, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $primary 'Documents\Plans\Recovery.md'), '# Recovery fixture', [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $primary @('add', '-A') | Out-Null
Invoke-ScratchGit $primary @('commit', '-m', 'fixture base') | Out-Null
$baseline = (@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()

$primaryOutput = Join-Path $primary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
New-Item -ItemType Directory -Force $primaryOutput | Out-Null
Copy-Item -LiteralPath $WorktreeCliExecutable -Destination (Join-Path $primaryOutput 'WorktreeCli.exe') -Force
Invoke-ScratchGit $primary @('worktree', 'add', '-b', 'fixture-session', $session, $baseline) | Out-Null
$sessionOutputParent = Join-Path $session 'Tools\WorktreeCli\Platforms\VisualStudio2026'
New-Item -ItemType Directory -Force $sessionOutputParent | Out-Null
New-Item -ItemType Junction -Path (Join-Path $sessionOutputParent 'Output') -Target $primaryOutput | Out-Null
[IO.File]::WriteAllText((Join-Path $session 'change.txt'), 'session change', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $session 'Documents\Plans\DigestBound.md'), '# Digest-bound add fixture', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $session 'Documents\Plans\GenericAdd.md'), '# Generic add fixture', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $session 'Documents\Plans\LandingOne.md'), '# Landing add fixture one', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $session 'Documents\Plans\LandingTwo.md'), '# Landing add fixture two', [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $session @('rm', 'Documents/Plans/Recovery.md') | Out-Null
Invoke-ScratchGit $session @('add', 'change.txt', 'Documents/Plans/DigestBound.md', 'Documents/Plans/GenericAdd.md', 'Documents/Plans/LandingOne.md', 'Documents/Plans/LandingTwo.md') | Out-Null
Invoke-ScratchGit $session @('commit', '-m', 'fixture change') | Out-Null
$approved = (@(Invoke-ScratchGit $session @('rev-parse', 'HEAD')))[0].Trim()

$owner = [guid]::NewGuid().ToString()
$fixtureEnvironment = [ordered]@{
	LOCALAPPDATA = $localAppData
	BROKEN_ENGINE_WORKTREE_PATH = $session
	BROKEN_ENGINE_SESSION_BRANCH = 'fixture-session'
	BROKEN_ENGINE_PRIMARY_CHECKOUT = $primary
	BROKEN_ENGINE_TARGET_BRANCH = 'main'
	BROKEN_ENGINE_BASELINE = $baseline
	BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER = $owner
}
foreach ($entry in $fixtureEnvironment.GetEnumerator()) {
	$previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key)
	[Environment]::SetEnvironmentVariable($entry.Key, $entry.Value)
}

$commonDirectory = ((@(Invoke-ScratchGit $primary @('rev-parse', '--path-format=absolute', '--git-common-dir')))[0].Trim())
Invoke-WorktreeCli @('plan', 'order', 'init', '--repo', $commonDirectory, '--worktree', $primary) | Out-Null
$requestRoot = Join-Path $session 'Temp\requests'
New-Item -ItemType Directory -Force $requestRoot | Out-Null
$digestRequestPath = Join-Path $requestRoot 'digest-bound.json'
$genericRequestPath = Join-Path $requestRoot 'generic.json'
$invalidRequestPath = Join-Path $requestRoot 'invalid.json'
$landingOneRequestPath = Join-Path $requestRoot 'landing-one.json'
$landingTwoRequestPath = Join-Path $requestRoot 'landing-two.json'
$digestRequest = '{"schemaVersion":1,"operation":"add","sequences":[[{"queue":"plans","plan":"Documents/Plans/DigestBound.md","tier":"Small","effort":1,"impact":1,"risks":1,"notes":"digest fixture"}]]}'
$genericRequest = '{"schemaVersion":1,"operation":"add","sequences":[[{"queue":"plans","plan":"Documents/Plans/GenericAdd.md","tier":"Small","effort":1,"impact":1,"risks":1,"notes":"generic fixture"}]]}'
$landingOneRequest = '{"schemaVersion":1,"operation":"add","sequences":[[{"queue":"plans","plan":"Documents/Plans/LandingOne.md","tier":"Small","effort":1,"impact":1,"risks":1,"notes":"landing fixture one"}]]}'
$landingTwoRequest = '{"schemaVersion":1,"operation":"add","sequences":[[{"queue":"plans","plan":"Documents/Plans/LandingTwo.md","tier":"Small","effort":1,"impact":1,"risks":1,"notes":"landing fixture two"}]]}'
[IO.File]::WriteAllText($digestRequestPath, $digestRequest, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($genericRequestPath, $genericRequest, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($invalidRequestPath, 'not-json', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($landingOneRequestPath, $landingOneRequest, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($landingTwoRequestPath, $landingTwoRequest, [Text.UTF8Encoding]::new($false))
$digestRequestSha256 = (Get-FileHash -LiteralPath $digestRequestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$landingOneRequestSha256 = (Get-FileHash -LiteralPath $landingOneRequestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$landingTwoRequestSha256 = (Get-FileHash -LiteralPath $landingTwoRequestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$malformedDigest = Invoke-WorktreeCli @('plan', 'order', 'add', '--repo', $commonDirectory, '--worktree', $session, '--owner', $owner, '--session', 'finalize-fixture', '--request', 'Temp/requests/missing.json', '--request-sha256', 'ABC') 1
Assert-True (($malformedDigest -join "`n").Contains('requires 64 lowercase hexadecimal characters', [StringComparison]::Ordinal)) 'malformed request digest fails at argument validation'
$wrongDigest = Invoke-WorktreeCli @('plan', 'order', 'add', '--repo', $commonDirectory, '--worktree', $session, '--owner', $owner, '--session', 'finalize-fixture', '--request', 'Temp/requests/invalid.json', '--request-sha256', ('0' * 64)) 1
Assert-True (($wrongDigest -join "`n").Contains('request SHA-256 does not match', [StringComparison]::Ordinal)) 'wrong request digest fails before JSON parsing'
$rejectedValidation = (Invoke-WorktreeCli @('plan', 'order', 'validate', '--repo', $commonDirectory, '--worktree', $session) | ConvertFrom-Json -Depth 100)
Assert-True (@($rejectedValidation.rows | Where-Object { $_.plan -ceq 'Documents/Plans/DigestBound.md' }).Count -eq 0) 'malformed and wrong request digests do not mutate queue'
Invoke-WorktreeCli @('plan', 'order', 'add', '--repo', $commonDirectory, '--worktree', $session, '--owner', $owner, '--session', 'finalize-fixture', '--request', 'Temp/requests/digest-bound.json', '--request-sha256', $digestRequestSha256) | Out-Null
Invoke-WorktreeCli @('plan', 'order', 'add', '--repo', $commonDirectory, '--worktree', $session, '--owner', $owner, '--session', 'finalize-fixture', '--request', 'Temp/requests/generic.json') | Out-Null
$acceptedValidation = (Invoke-WorktreeCli @('plan', 'order', 'validate', '--repo', $commonDirectory, '--worktree', $session) | ConvertFrom-Json -Depth 100)
Assert-True (@($acceptedValidation.rows | Where-Object { $_.plan -ceq 'Documents/Plans/DigestBound.md' }).Count -eq 1) 'correct request digest permits add'
Assert-True (@($acceptedValidation.rows | Where-Object { $_.plan -ceq 'Documents/Plans/GenericAdd.md' }).Count -eq 1) 'generic add remains compatible without request digest'
Assert-True (@($acceptedValidation.rows | Where-Object { $_.plan -in @('Documents/Plans/LandingOne.md', 'Documents/Plans/LandingTwo.md') }).Count -eq 0) 'landing requests are previously unqueued'
Invoke-WorktreeCli @('plan', 'queue', 'lock', '--repo', $commonDirectory, '--order', 'Documents/Plans/Order.md', '--owner', $owner, '--session', 'finalize-fixture', '--worktree', $session) | Out-Null
Invoke-WorktreeCli @('plan', 'row', 'claim', '--repo', $commonDirectory, '--order', 'Documents/Plans/Order.md', '--plan', 'Recovery.md', '--owner', $owner, '--session', 'finalize-fixture', '--worktree', $session) | Out-Null
Invoke-WorktreeCli @('plan', 'queue', 'unlock', '--repo', $commonDirectory, '--order', 'Documents/Plans/Order.md', '--owner', $owner) | Out-Null
Register-WorktreeCliSession -RepositoryRoot $primary -Owner $owner -Label 'finalize-fixture' -Worktree $session -LegacySessionsClosed | Out-Null
$ownerRegistered = $true
try {
	$commonPreflight = @(
		'-Mode', 'session-landing', '-Checkpoint', 'after-reconciliation',
		'-CurrentWorktree', $session, '-PrimaryWorktree', $primary,
		'-CurrentBranch', 'fixture-session', '-PrimaryBranch', 'main',
		'-Baseline', $baseline, '-ExpectedCurrentTip', $approved,
		'-ExpectedPrimaryTip', $baseline, '-SessionOwner', $owner,
		'-WaitSeconds', '5'
	)
	$run = Invoke-JsonScript $preflightScript (@($commonPreflight) + @('-PlanAddRequestDisposition', 'none'))
	Assert-Outcome $run 'preflight-none' 0 'pass' 'ok'

	$requestPath = Join-Path $session 'Temp\requests\add.json'
	New-Item -ItemType Directory -Force (Split-Path -Parent $requestPath) | Out-Null
	[IO.File]::WriteAllText($requestPath, '{"schemaVersion":1,"operation":"add"}', [Text.UTF8Encoding]::new($false))
	$requestSha256 = (Get-FileHash -LiteralPath $requestPath -Algorithm SHA256).Hash.ToLowerInvariant()
	$run = Invoke-JsonScript $preflightScript (@($commonPreflight) + @('-PlanAddRequestDisposition', 'list', '-PlanAddRequestPaths', 'Temp/requests/add.json'))
	Assert-Outcome $run 'preflight-list' 0 'pass' 'ok'
	if ($null -ne $run.Json) {
		Assert-True ($run.Json.planAddRequests.items.Count -eq 1) 'preflight-list returned one request identity'
		Assert-True ($run.Json.planAddRequests.items[0].sha256 -ceq $requestSha256) 'preflight-list returned request SHA-256'
		Assert-True ($run.Json.worktreeCli.requiredCapabilities -contains 'plan:order:add-request-sha256,validate') 'preflight-list required digest-bound add/validate capability'
	}
	$mutationPreflight = @($commonPreflight)
	$mutationPreflight[3] = 'pre-mutation'
	$run = Invoke-JsonScript $preflightScript (@($mutationPreflight) + @('-PlanAddRequestDisposition', 'list', '-PlanAddRequestPaths', 'Temp/requests/add.json', '-PlanAddRequestSha256', ('0' * 64)))
	Assert-Outcome $run 'preflight-request-tamper' 2 'blocked' 'plan-add-request.identity-changed'
	$run = Invoke-JsonScript $preflightScript (@($mutationPreflight) + @('-PlanAddRequestDisposition', 'list', '-PlanAddRequestItemsJson', '[{"path":""}]'))
	Assert-Outcome $run 'preflight-blank-request-path' 1 'error' 'input.invalid'
	$run = Invoke-JsonScript $preflightScript (@($mutationPreflight) + @('-PlanAddRequestDisposition', 'list', '-PlanAddRequestItemsJson', '[{"path":"Temp/requests/landing-one.json","sha256":""}]'))
	Assert-Outcome $run 'preflight-blank-request-hash' 1 'error' 'input.invalid'
	$run = Invoke-JsonScript $preflightScript (@($mutationPreflight) + @('-PlanAddRequestDisposition', 'list', '-PlanAddRequestItemsJson', '[{"path":"Temp/requests/landing-one.json","sha256":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"}]'))
	Assert-Outcome $run 'preflight-uppercase-request-hash' 1 'error' 'input.invalid'

	$landingParameters = [ordered]@{
		CurrentWorktree = $session
		PrimaryWorktree = $primary
		CurrentBranch = 'fixture-session'
		PrimaryBranch = 'main'
		Baseline = $baseline
		ExpectedCurrentTip = $approved
		ExpectedPrimaryTip = $baseline
		SessionOwner = $owner
		SessionLabel = 'finalize-fixture'
		ApprovedSessionCommit = $approved
		HasPlanRowClaim = $true
		PlanOrder = 'Documents/Plans/Order.md'
		Plan = 'Recovery.md'
		PlanAddRequestDisposition = 'list'
		PlanAddRequestPaths = @('Temp/requests/landing-one.json', 'Temp/requests/landing-two.json')
		PlanAddRequestSha256 = @($landingOneRequestSha256, $landingTwoRequestSha256)
	}
	$run = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
	Assert-Outcome $run 'landing' 0 'landed' 'ok'
	if ($null -ne $run.Json) {
		Assert-True $run.Json.queuePublication.completed 'landing completed plan queue publication'
		Assert-True (@($run.Json.queuePublication.added).Count -eq 2) 'landing published both approval-bound requests'
		Assert-True $run.Json.planRow.released 'landing released plan-row claim'
	}
	Assert-True ((@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim() -ceq $approved) 'landing advanced scratch primary only'
	$rowStatus = Invoke-WorktreeCli @('plan', 'row', 'status', '--repo', $commonDirectory, '--order', 'Documents/Plans/Order.md', '--plan', 'Recovery.md', '--owner', $owner) 2
	Assert-True ((($rowStatus -join "`n") | ConvertFrom-Json).held -eq $false) 'landing left plan-row claim strictly absent'
	$terminalValidation = Invoke-WorktreeCli @('plan', 'order', 'validate', '--repo', $commonDirectory, '--worktree', $primary)
	$terminalQueue = ($terminalValidation -join "`n") | ConvertFrom-Json -Depth 100
	Assert-True $terminalQueue.ok 'landing left terminal queue validation passing'
	Assert-True (@($terminalQueue.rows | Where-Object { $_.plan -ceq 'Documents/Plans/Recovery.md' }).Count -eq 0) 'landing left canonical target row absent'
	Assert-True (@($terminalQueue.rows | Where-Object { $_.plan -ceq 'Documents/Plans/LandingOne.md' }).Count -eq 1) 'landing published first previously unqueued row exactly once'
	Assert-True (@($terminalQueue.rows | Where-Object { $_.plan -ceq 'Documents/Plans/LandingTwo.md' }).Count -eq 1) 'landing published second previously unqueued row exactly once'
	Assert-True (-not (Test-Path -LiteralPath (Join-Path $primary 'Documents/Plans/Recovery.md'))) 'landing left canonical primary plan file absent'
	$run = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
	Assert-Outcome $run 'post-unclaim-recovery' 0 'landed' 'ok'
	if ($null -ne $run.Json) {
		Assert-True $run.Json.queuePublication.complete.alreadyTerminal 'post-unclaim recovery proved terminal plan completion'
		Assert-True $run.Json.queuePublication.validated 'post-unclaim recovery reran final queue validation'
		Assert-True $run.Json.planRow.released 'post-unclaim recovery treated proven-absent row claim as released'
	}
	$recoveryValidation = (Invoke-WorktreeCli @('plan', 'order', 'validate', '--repo', $commonDirectory, '--worktree', $primary) | ConvertFrom-Json -Depth 100)
	Assert-True (@($recoveryValidation.rows | Where-Object { $_.plan -ceq 'Documents/Plans/LandingOne.md' }).Count -eq 1) 'recovery keeps first request idempotent'
	Assert-True (@($recoveryValidation.rows | Where-Object { $_.plan -ceq 'Documents/Plans/LandingTwo.md' }).Count -eq 1) 'recovery keeps second request idempotent'
}
finally {
	try { Unregister-WorktreeCliSession -RepositoryRoot $primary -Owner $owner } catch { }
	$ownerRegistered = $false
}

Write-Host ''
if ($script:Failures.Count -gt 0) {
	Write-Host "Finalize workflow fixtures FAILED ($($script:Failures.Count) assertion(s))."
	$fixtureExitCode = 1
}
else {
	Write-Host 'Finalize workflow fixtures passed.'
}
}
finally {
	if ($ownerRegistered -and $null -ne $owner) {
		try { Unregister-WorktreeCliSession -RepositoryRoot $primary -Owner $owner } catch { }
	}
	if ($null -ne $fixtureEnvironment) {
		foreach ($entry in $fixtureEnvironment.GetEnumerator()) { [Environment]::SetEnvironmentVariable($entry.Key, $previousEnvironment[$entry.Key]) }
	}
	$validatedScratch = Assert-SafeScratchRoot $scratchParent $scratchBase $scratchLeaf
	if (Test-Path -LiteralPath $validatedScratch) {
		Remove-Item -LiteralPath $validatedScratch -Recurse -Force -Confirm:$false
	}
}
exit $fixtureExitCode

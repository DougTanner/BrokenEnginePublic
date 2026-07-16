[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $Executable
)

$ErrorActionPreference = 'Stop'

$script:Executable = (Get-Item -LiteralPath $Executable -Force -ErrorAction Stop).FullName
$script:RepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$script:FixtureRoot = Join-Path $script:RepositoryRoot "Temp/WorktreeCliPlanOrderFixtures/$([Guid]::NewGuid().ToString('N'))"
$script:Failures = [Collections.Generic.List[string]]::new()
$script:Passed = [Collections.Generic.List[string]]::new()
$script:Claims = [Collections.Generic.List[object]]::new()
$script:QueueLocks = [Collections.Generic.List[object]]::new()
$script:Utf8 = [Text.UTF8Encoding]::new($false, $true)

function Invoke-Process([string] $FilePath, [string[]] $Arguments, [string] $WorkingDirectory = $script:RepositoryRoot) {
	$startInfo = [Diagnostics.ProcessStartInfo]::new()
	$startInfo.FileName = $FilePath
	$startInfo.WorkingDirectory = $WorkingDirectory
	$startInfo.UseShellExecute = $false
	$startInfo.CreateNoWindow = $true
	$startInfo.RedirectStandardOutput = $true
	$startInfo.RedirectStandardError = $true
	foreach ($argument in $Arguments) { [void] $startInfo.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $startInfo
	if (-not $process.Start()) { throw "Could not start '$FilePath'." }
	$stdout = $process.StandardOutput.ReadToEndAsync()
	$stderr = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$result = [pscustomobject]@{
		ExitCode = $process.ExitCode
		Stdout = $stdout.GetAwaiter().GetResult()
		Stderr = $stderr.GetAwaiter().GetResult()
	}
	$process.Dispose()
	return $result
}

function Invoke-Git([string] $WorkingDirectory, [string[]] $Arguments, [int] $ExpectedExitCode = 0) {
	$result = Invoke-Process -FilePath 'git.exe' -Arguments $Arguments -WorkingDirectory $WorkingDirectory
	if ($result.ExitCode -ne $ExpectedExitCode) {
		throw "git $($Arguments -join ' ') exited $($result.ExitCode), expected $ExpectedExitCode.`n$($result.Stdout)$($result.Stderr)"
	}
	return $result.Stdout.Trim()
}

function Get-ArgumentValue([string[]] $Arguments, [string] $Name) {
	for ($index = 0; $index -lt ($Arguments.Count - 1); ++$index) {
		if ($Arguments[$index] -ceq $Name) { return [string] $Arguments[$index + 1] }
	}
	return ''
}

function Register-QueueLock([string] $Repository, [string] $Order, [string] $Owner, [string] $Session) {
	$script:QueueLocks.Add([pscustomobject]@{ Repository = $Repository; Order = $Order; Owner = $Owner; Session = $Session })
}

function Register-PlanOrderQueueLocks([string[]] $Arguments) {
	if ($Arguments.Count -lt 3 -or $Arguments[0] -cne 'plan' -or $Arguments[1] -cne 'order' -or $Arguments[2] -cnotin @('add', 'update', 'complete', 'claim-next')) { return }

	$repository = Get-ArgumentValue $Arguments '--repo'
	$owner = Get-ArgumentValue $Arguments '--owner'
	$session = Get-ArgumentValue $Arguments '--session'
	if ([string]::IsNullOrEmpty($repository) -or [string]::IsNullOrEmpty($owner) -or [string]::IsNullOrEmpty($session)) {
		throw "Plan-order fixture cannot pretrack $($Arguments[2]) queue locks without repo, owner, and session."
	}

	foreach ($order in @('Documents/Features/Order.md', 'Documents/Plans/Order.md')) {
		Register-QueueLock $repository $order $owner $session
	}
}

function Invoke-WorktreeCli([string[]] $Arguments, [int] $ExpectedExitCode) {
	Register-PlanOrderQueueLocks $Arguments
	$result = Invoke-Process -FilePath $script:Executable -Arguments $Arguments
	if ($result.ExitCode -ne $ExpectedExitCode) {
		throw "WorktreeCli $($Arguments -join ' ') exited $($result.ExitCode), expected $ExpectedExitCode.`nstdout: $($result.Stdout)`nstderr: $($result.Stderr)"
	}
	return $result
}

function ConvertFrom-AgentJson($Result) {
	try { return ($Result.Stdout.Trim() | ConvertFrom-Json -Depth 100 -ErrorAction Stop) }
	catch { throw "WorktreeCli stdout was not one JSON value:`n$($Result.Stdout)`nstderr:`n$($Result.Stderr)" }
}

function Assert-True([bool] $Condition, [string] $Message) {
	if (-not $Condition) { throw $Message }
}

function Assert-Equal($Actual, $Expected, [string] $Message) {
	if ($Actual -cne $Expected) { throw "$Message Expected '$Expected', got '$Actual'." }
}

function Assert-Bytes([string] $Path, [byte[]] $Expected, [string] $Message) {
	$actual = [IO.File]::ReadAllBytes($Path)
	if (-not [Collections.StructuralComparisons]::StructuralEqualityComparer.Equals($actual, $Expected)) {
		throw "$Message Exact bytes differ for '$Path'."
	}
}

function Set-Utf8File([string] $Path, [string] $Text) {
	[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
	[IO.File]::WriteAllText($Path, $Text, $script:Utf8)
}

function Set-RawFile([string] $Path, [byte[]] $Bytes) {
	[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
	[IO.File]::WriteAllBytes($Path, $Bytes)
}

function Get-Sha256([string] $Path) {
	return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function New-Row(
	[string] $Plan,
	[string] $Tier = 'Small',
	[int] $Effort = 2,
	[int] $Impact = 3,
	[int] $Risks = 1,
	[string[]] $DependsOn = @(),
	[string] $Notes = 'fixture'
) {
	return [pscustomobject]@{
		Plan = $Plan; Tier = $Tier; Effort = $Effort; Impact = $Impact; Risks = $Risks
		DependsOn = @($DependsOn); Notes = $Notes
	}
}

function Get-OrderText([string] $Queue, [object[]] $Rows, [string] $Newline, [string] $ReferenceName) {
	$directory = "Documents/$Queue"
	$lines = [Collections.Generic.List[string]]::new()
	$lines.Add("# $Queue plan order")
	$lines.Add('')
	$lines.Add('| Plan | Tier | Effort | Impact | Risks | Score | Depends On | Notes |')
	$lines.Add('| --- | --- | --- | --- | --- | --- | --- | --- |')
	foreach ($row in $Rows) {
		Assert-True $row.Plan.StartsWith("$directory/", [StringComparison]::Ordinal) "Row '$($row.Plan)' does not belong to $Queue."
		$relative = $row.Plan.Substring($directory.Length + 1)
		$dependencies = if ($row.DependsOn.Count -eq 0) { '-' } else { $row.DependsOn -join '; ' }
		$score = $row.Effort - $row.Impact + $row.Risks
		$lines.Add("| [$relative]($relative) | $($row.Tier) | $($row.Effort) | $($row.Impact) | $($row.Risks) | $score | $dependencies | $($row.Notes) |")
	}
	$lines.Add('')
	$lines.Add('### Reference / Index Documents')
	$lines.Add('')
	$lines.Add('| Document | Purpose |')
	$lines.Add('| --- | --- |')
	$lines.Add("| [$ReferenceName]($ReferenceName) | Fixture reference |")
	$lines.Add('')
	return ($lines -join $Newline) + $Newline
}

function New-FixtureRepository(
	[string] $Name,
	[object[]] $PlansRows = @(),
	[object[]] $FeaturesRows = @(),
	[string] $PlansNewline = "`n",
	[string] $FeaturesNewline = "`n",
	[hashtable] $PlanContents = @{}
) {
	$primary = Join-Path $script:FixtureRoot "$Name/primary"
	[IO.Directory]::CreateDirectory($primary) | Out-Null
	Invoke-Git $primary @('init', '--initial-branch=main', '.') | Out-Null
	Invoke-Git $primary @('config', 'user.email', 'worktreecli-fixture@example.invalid') | Out-Null
	Invoke-Git $primary @('config', 'user.name', 'WorktreeCli Fixture') | Out-Null
	Invoke-Git $primary @('config', 'core.autocrlf', 'false') | Out-Null
	Invoke-Git $primary @('config', 'core.eol', 'lf') | Out-Null
	Set-Utf8File (Join-Path $primary 'Documents/Plans/Order.md') (Get-OrderText 'Plans' $PlansRows $PlansNewline 'Reference.md')
	Set-Utf8File (Join-Path $primary 'Documents/Features/Order.md') (Get-OrderText 'Features' $FeaturesRows $FeaturesNewline 'Reference.txt')
	Set-Utf8File (Join-Path $primary 'Documents/Plans/Reference.md') "# Plans reference$PlansNewline"
	Set-Utf8File (Join-Path $primary 'Documents/Features/Reference.txt') "Features reference$FeaturesNewline"
	Set-Utf8File (Join-Path $primary 'Documents/Plans/AGENTS.md') '# Plans instructions'
	Set-Utf8File (Join-Path $primary 'Documents/Plans/CLAUDE.md') '@AGENTS.md'
	Set-Utf8File (Join-Path $primary 'Documents/Features/AGENTS.md') '# Features instructions'
	Set-Utf8File (Join-Path $primary 'Documents/Features/CLAUDE.md') '@AGENTS.md'
	foreach ($row in @($PlansRows) + @($FeaturesRows)) {
		$content = if ($PlanContents.ContainsKey($row.Plan)) { [string] $PlanContents[$row.Plan] } else { "# $($row.Plan)`n" }
		Set-Utf8File (Join-Path $primary $row.Plan) $content
	}
	Invoke-Git $primary @('add', '--all') | Out-Null
	Invoke-Git $primary @('commit', '-m', 'fixture baseline') | Out-Null
	$common = Invoke-Git $primary @('rev-parse', '--path-format=absolute', '--git-common-dir')
	return [pscustomobject]@{ Primary = $primary; Common = [IO.Path]::GetFullPath($common); Branch = 'main'; Name = $Name }
}

function Add-Worktree($Fixture, [string] $Name) {
	$path = Join-Path ([IO.Path]::GetDirectoryName($Fixture.Primary)) $Name
	Invoke-Git $Fixture.Primary @('worktree', 'add', '--detach', $path, 'HEAD') | Out-Null
	return $path
}

function Get-OrderArguments($Fixture, [string] $Verb, [string] $Worktree) {
	return @('plan', 'order', $Verb, '--repo', $Fixture.Common, '--worktree', $Worktree)
}

function ConvertTo-OrderRelativePlan([string] $Order, [string] $Plan) {
	$directory = [IO.Path]::GetDirectoryName($Order).Replace('\', '/')
	$prefix = "$directory/"
	Assert-True $Plan.StartsWith($prefix, [StringComparison]::Ordinal) "Plan '$Plan' is not beneath Order '$Order'."
	return $Plan.Substring($prefix.Length)
}

function Write-Request([string] $Worktree, [string] $Name, $Request) {
	$relative = "Temp/$Name.json"
	Set-Utf8File (Join-Path $Worktree $relative) ($Request | ConvertTo-Json -Depth 100 -Compress)
	return $relative
}

function Get-Validation($Fixture, [string] $Worktree, [int] $ExpectedExitCode = 0) {
	$result = Invoke-WorktreeCli (Get-OrderArguments $Fixture 'validate' $Worktree) $ExpectedExitCode
	return [pscustomobject]@{ Result = $result; Json = ConvertFrom-AgentJson $result }
}

function Get-RowHash($Fixture, [string] $Worktree, [string] $Plan) {
	$validation = Get-Validation $Fixture $Worktree
	$row = @($validation.Json.rows | Where-Object plan -CEQ $Plan)
	Assert-Equal $row.Count 1 "Validation did not return exactly one row for $Plan."
	return [string] $row[0].rowSha256
}

function Invoke-QueueLock($Fixture, [string] $Order, [string] $Owner, [string] $Session, [string] $Worktree) {
	Register-QueueLock $Fixture.Common $Order $Owner $Session
	return Invoke-WorktreeCli @('plan', 'queue', 'lock', '--repo', $Fixture.Common, '--order', $Order, '--owner', $Owner, '--session', $Session, '--worktree', $Worktree) 0
}

function Invoke-QueueUnlock($Fixture, [string] $Order, [string] $Owner) {
	Invoke-WorktreeCli @('plan', 'queue', 'unlock', '--repo', $Fixture.Common, '--order', $Order, '--owner', $Owner) 0 | Out-Null
}

function Invoke-RowClaim($Fixture, [string] $Order, [string] $Plan, [string] $Owner, [string] $Session, [string] $Worktree) {
	Assert-True (-not $Plan.StartsWith('Documents/', [StringComparison]::Ordinal)) 'Legacy plan row claim requires an Order-relative plan identity.'
	Invoke-QueueLock $Fixture $Order $Owner $Session $Worktree | Out-Null
	try {
		Invoke-WorktreeCli @('plan', 'row', 'claim', '--repo', $Fixture.Common, '--order', $Order, '--plan', $Plan, '--owner', $Owner, '--session', $Session, '--worktree', $Worktree) 0 | Out-Null
		$status = ConvertFrom-AgentJson (Invoke-WorktreeCli @('plan', 'row', 'status', '--repo', $Fixture.Common, '--order', $Order, '--plan', $Plan, '--owner', $Owner) 0)
		Assert-True ($status.owner -ceq $Owner -and $status.ownedByRequester) 'Legacy plan row status did not resolve the Order-relative claim identity.'
		$script:Claims.Add([pscustomobject]@{ Fixture=$Fixture; Order=$Order; Plan=$Plan; Owner=$Owner })
	}
	finally { Invoke-QueueUnlock $Fixture $Order $Owner }
}

function Invoke-RowUnclaim($Fixture, [string] $Order, [string] $Plan, [string] $Owner) {
	Assert-True (-not $Plan.StartsWith('Documents/', [StringComparison]::Ordinal)) 'Legacy plan row unclaim requires an Order-relative plan identity.'
	$status = ConvertFrom-AgentJson (Invoke-WorktreeCli @('plan', 'row', 'status', '--repo', $Fixture.Common, '--order', $Order, '--plan', $Plan, '--owner', $Owner) 0)
	Assert-True ($status.owner -ceq $Owner -and $status.ownedByRequester) 'Legacy plan row status did not resolve the Order-relative unclaim identity.'
	Invoke-WorktreeCli @('plan', 'row', 'unclaim', '--repo', $Fixture.Common, '--order', $Order, '--plan', $Plan, '--owner', $Owner) 0 | Out-Null
}

function Invoke-ClaimNext($Fixture, [string] $Worktree, [string] $Owner, [string] $Queue, [int] $ExpectedExitCode, [string] $Plan = '') {
	$arguments = @('plan', 'order', 'claim-next', '--repo', $Fixture.Common, '--primary-worktree', $Fixture.Primary, '--worktree', $Worktree, '--branch', $Fixture.Branch, '--owner', $Owner, '--session', "session-$Owner", '--queue', $Queue)
	if ($Plan) { $arguments += @('--plan', $Plan) }
	$result = Invoke-WorktreeCli $arguments $ExpectedExitCode
	$json = ConvertFrom-AgentJson $result
	if ($ExpectedExitCode -eq 0 -and $json.claimed) {
		$order = if ($Queue -ceq 'plans') { 'Documents/Plans/Order.md' } else { 'Documents/Features/Order.md' }
		$script:Claims.Add([pscustomobject]@{ Fixture=$Fixture; Order=$order; Plan=(ConvertTo-OrderRelativePlan $order ([string] $json.plan)); Owner=$Owner })
	}
	return [pscustomobject]@{ Result = $result; Json = $json }
}

function Invoke-Case([string] $Name, [scriptblock] $Body) {
	try {
		& $Body
		$script:Passed.Add($Name)
		Write-Host "PASS $Name"
	}
	catch {
		$script:Failures.Add("$Name`: $($_.Exception.Message)")
		Write-Warning "FAIL $Name`: $($_.Exception.Message)"
	}
}

function Clear-TrackedQueueLocks {
	for ($index = $script:QueueLocks.Count - 1; $index -ge 0; --$index) {
		$lock = $script:QueueLocks[$index]
		try {
			$statusResult = Invoke-Process -FilePath $script:Executable -Arguments @('plan', 'queue', 'status', '--repo', $lock.Repository, '--order', $lock.Order, '--owner', $lock.Owner)
			$status = ConvertFrom-AgentJson $statusResult
			if ($status.ownedByRequester) {
				$unlockResult = Invoke-Process -FilePath $script:Executable -Arguments @('plan', 'queue', 'unlock', '--repo', $lock.Repository, '--order', $lock.Order, '--owner', $lock.Owner)
				if ($unlockResult.ExitCode -ne 0) {
					Write-Warning "Could not release tracked queue lock '$($lock.Order)' for '$($lock.Owner)': $($unlockResult.Stdout)$($unlockResult.Stderr)"
				}
			}
		}
		catch {
			Write-Warning "Could not inspect tracked queue lock '$($lock.Order)' for '$($lock.Owner)': $($_.Exception.Message)"
		}
	}
}

function Assert-Diagnostic([string] $Name, [scriptblock] $Mutation, [string[]] $Codes) {
	$fixture = New-FixtureRepository -Name "diagnostic-$Name" -PlansRows @((New-Row 'Documents/Plans/A.md')) -FeaturesRows @((New-Row 'Documents/Features/F.md'))
	& $Mutation $fixture.Primary
	$first = Get-Validation $fixture $fixture.Primary 2
	$second = Get-Validation $fixture $fixture.Primary 2
	Assert-Equal $first.Result.Stdout $second.Result.Stdout "$Name diagnostics were not byte-deterministic."
	$actualCodes = @($first.Json.diagnostics.code)
	foreach ($code in $Codes) { Assert-True ($actualCodes -ccontains $code) "$Name did not diagnose '$code'; got '$($actualCodes -join ', ')'." }
}

$executableItem = Get-Item -LiteralPath $script:Executable -Force
if ($executableItem.PSIsContainer -or ($executableItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $executableItem.Length -eq 0) {
	throw "WorktreeCli executable must be a nonempty ordinary file: '$script:Executable'."
}
$help = Invoke-WorktreeCli @('--help') 0
Assert-True $help.Stdout.Contains('WorktreeCli.exe plan order validate', [StringComparison]::Ordinal) 'WorktreeCli does not advertise plan order; build it before running this fixture.'

[IO.Directory]::CreateDirectory($script:FixtureRoot) | Out-Null
try {
	Invoke-Case 'validate both queues, references, Unicode, newline preservation, and deterministic JSON' {
		$plans = @(
			New-Row 'Documents/Plans/Zeta.md' 'Small' 10 1 1 @() 'approximate order | is allowed'
			New-Row 'Documents/Plans/Alpha.md' 'Small' 2 3 1 @('Documents/Features/Foundation.txt') 'depends cross-queue'
		)
		$features = @(New-Row 'Documents/Features/Foundation.txt' 'Architectural' 5 4 3 @() 'naive cafe')
		$fixture = New-FixtureRepository 'validate-valid' $plans $features "`r`n" "`n" @{ 'Documents/Plans/Alpha.md' = "# Café 漢字`r`n" }
		$plansBefore = [IO.File]::ReadAllBytes((Join-Path $fixture.Primary 'Documents/Plans/Order.md'))
		$featuresBefore = [IO.File]::ReadAllBytes((Join-Path $fixture.Primary 'Documents/Features/Order.md'))
		$first = Get-Validation $fixture $fixture.Primary
		$second = Get-Validation $fixture $fixture.Primary
		Assert-True $first.Json.ok 'Valid fixture did not validate.'
		Assert-Equal $first.Result.Stdout $second.Result.Stdout 'Valid JSON output was not deterministic.'
		Assert-Equal @($first.Json.rows).Count 3 'Valid fixture row count differs.'
		Assert-Bytes (Join-Path $fixture.Primary 'Documents/Plans/Order.md') $plansBefore 'Validate mutated Plans.'
		Assert-Bytes (Join-Path $fixture.Primary 'Documents/Features/Order.md') $featuresBefore 'Validate mutated Features.'
	}

	Invoke-Case 'validate negative diagnostics' {
		Assert-Diagnostic 'orphan' { param($root) Set-Utf8File (Join-Path $root 'Documents/Plans/Orphan.md') '# orphan' } @('orphan-plan')
		Assert-Diagnostic 'missing' { param($root) Remove-Item -LiteralPath (Join-Path $root 'Documents/Plans/A.md') -Force } @('missing-plan-file')
		Assert-Diagnostic 'duplicate' { param($root) $path=Join-Path $root 'Documents/Plans/Order.md'; $text=[IO.File]::ReadAllText($path); $rows=@($text -split "`n" | Where-Object { $_.StartsWith('| [A.md](A.md) ', [StringComparison]::Ordinal) }); $row=$rows[0].TrimEnd([char] "`r"); Set-Utf8File $path ($text.Replace("$row`n", "$row`n$row`n")) } @('duplicate-plan')
		Assert-Diagnostic 'score' { param($root) $path=Join-Path $root 'Documents/Plans/Order.md'; $text=[IO.File]::ReadAllText($path); Set-Utf8File $path ($text.Replace('| 2 | 3 | 1 | 0 |', '| 2 | 3 | 1 | 99 |')) } @('invalid-score')
		Assert-Diagnostic 'malformed' { param($root) $path=Join-Path $root 'Documents/Plans/Order.md'; $text=[IO.File]::ReadAllText($path); Set-Utf8File $path ($text.Replace('| [A.md](A.md) | Small | 2 | 3 | 1 | 0 | - | fixture |', '| malformed |')) } @('malformed-row')
		Assert-Diagnostic 'unsafe' { param($root) $path=Join-Path $root 'Documents/Plans/Order.md'; $text=[IO.File]::ReadAllText($path); Set-Utf8File $path ($text.Replace('[A.md](A.md)', '[Escape.md](../Escape.md)')) } @('invalid-plan-path')
		Assert-Diagnostic 'missing-dependency' { param($root) $path=Join-Path $root 'Documents/Plans/Order.md'; $text=[IO.File]::ReadAllText($path); Set-Utf8File $path ($text.Replace('| - | fixture |', '| Documents/Features/Missing.md | fixture |')) } @('missing-dependency')
		Assert-Diagnostic 'duplicate-dependency' { param($root) $path=Join-Path $root 'Documents/Plans/Order.md'; $text=[IO.File]::ReadAllText($path); Set-Utf8File $path ($text.Replace('| - | fixture |', '| Documents/Features/F.md; Documents/Features/F.md | fixture |')) } @('duplicate-dependency')
		Assert-Diagnostic 'self-dependency' { param($root) $path=Join-Path $root 'Documents/Plans/Order.md'; $text=[IO.File]::ReadAllText($path); Set-Utf8File $path ($text.Replace('| - | fixture |', '| Documents/Plans/A.md | fixture |')) } @('self-dependency')
		Assert-Diagnostic 'cycle' { param($root) foreach($relative in @('Documents/Plans/Order.md','Documents/Features/Order.md')) { $path=Join-Path $root $relative; $text=[IO.File]::ReadAllText($path); $dependency=if($relative.StartsWith('Documents/Plans')){'Documents/Features/F.md'}else{'Documents/Plans/A.md'}; Set-Utf8File $path ($text.Replace('| - | fixture |', "| $dependency | fixture |")) } } @('dependency-cycle')
		Assert-Diagnostic 'reference-overlap' { param($root) $path=Join-Path $root 'Documents/Plans/Order.md'; $text=[IO.File]::ReadAllText($path); Set-Utf8File $path ($text.Replace('[Reference.md](Reference.md)', '[A.md](A.md)')) } @('reference-overlap')
		Assert-Diagnostic 'missing-reference' { param($root) Remove-Item -LiteralPath (Join-Path $root 'Documents/Plans/Reference.md') -Force } @('missing-reference')
		Assert-Diagnostic 'invalid-utf8' { param($root) $path=Join-Path $root 'Documents/Plans/Order.md'; Set-RawFile $path ([byte[]](0xff,0xfe,0xfd)) } @('invalid-utf8')
	}

	Invoke-Case 'add independent sequences and prerequisite chains atomically' {
		$live = New-Row 'Documents/Features/Live.txt'
		$fixture = New-FixtureRepository 'add' @() @($live) "`r`n" "`n"
		$session = Add-Worktree $fixture 'session'
		foreach ($entry in @(
			@('Documents/Plans/First.md', "# First café`r`n"),
			@('Documents/Features/Second.txt', "Second 漢字`n"),
			@('Documents/Plans/Third.md', "# Third`n"),
			@('Documents/Plans/Independent.md', "# Independent`n")
		)) { Set-Utf8File (Join-Path $session $entry[0]) $entry[1] }
		$request = @{
			schemaVersion = 1; operation = 'add'; sequences = @(
				@(
					@{ queue='plans'; plan='Documents/Plans/First.md'; tier='Small'; effort=4; impact=3; risks=1; notes='first | pipe'; dependsOn=@('Documents/Features/Live.txt') },
					@{ queue='features'; plan='Documents/Features/Second.txt'; tier='Medium'; effort=5; impact=3; risks=2; notes='second'; dependsOn=@() },
					@{ queue='plans'; plan='Documents/Plans/Third.md'; tier='Large'; effort=7; impact=4; risks=2; notes='third'; dependsOn=@() }
				),
				@(@{ queue='plans'; plan='Documents/Plans/Independent.md'; tier='Small'; effort=1; impact=2; risks=1; notes='independent'; dependsOn=@() })
			)
		}
		$requestPath = Write-Request $session 'add' $request
		$result = Invoke-WorktreeCli ((Get-OrderArguments $fixture 'add' $session) + @('--owner','add-owner','--session','add-session','--request',$requestPath)) 0
		$json = ConvertFrom-AgentJson $result
		Assert-True ($json.handled -and $json.operation -ceq 'add') 'Add receipt is malformed.'
		Assert-Equal @($json.unlockResults).Count 2 'Add receipt omitted queue unlock results.'
		Assert-Equal $json.unlockResults[0].order 'Documents/Plans/Order.md' 'Queues were not released in reverse canonical order.'
		Assert-Equal $json.unlockResults[1].order 'Documents/Features/Order.md' 'Queues were not released in reverse canonical order.'
		Assert-True ($json.unlockResults[0].released -and $json.unlockResults[1].released) 'Add did not release both queue locks.'
		$expectedPlans = @(
			New-Row 'Documents/Plans/First.md' 'Small' 4 3 1 @('Documents/Features/Live.txt') 'first | pipe'
			New-Row 'Documents/Plans/Third.md' 'Large' 7 4 2 @('Documents/Features/Second.txt') 'third'
			New-Row 'Documents/Plans/Independent.md' 'Small' 1 2 1 @() 'independent'
		)
		$expectedFeatures = @(
			$live
			New-Row 'Documents/Features/Second.txt' 'Medium' 5 3 2 @('Documents/Plans/First.md') 'second'
		)
		Assert-Bytes (Join-Path $session 'Documents/Plans/Order.md') $script:Utf8.GetBytes((Get-OrderText 'Plans' $expectedPlans "`r`n" 'Reference.md')) 'Add Plans bytes differ.'
		Assert-Bytes (Join-Path $session 'Documents/Features/Order.md') $script:Utf8.GetBytes((Get-OrderText 'Features' $expectedFeatures "`n" 'Reference.txt')) 'Add Features bytes differ.'
		Assert-True (Get-Validation $fixture $session).Json.ok 'Added graph did not validate.'

		$plansBefore = [IO.File]::ReadAllBytes((Join-Path $session 'Documents/Plans/Order.md'))
		$featuresBefore = [IO.File]::ReadAllBytes((Join-Path $session 'Documents/Features/Order.md'))
		Set-Utf8File (Join-Path $session 'Temp/malformed.json') '{broken'
		Invoke-WorktreeCli ((Get-OrderArguments $fixture 'add' $session) + @('--owner','bad-owner','--session','bad-session','--request','Temp/malformed.json')) 1 | Out-Null
		Assert-Bytes (Join-Path $session 'Documents/Plans/Order.md') $plansBefore 'Malformed add mutated Plans.'
		Assert-Bytes (Join-Path $session 'Documents/Features/Order.md') $featuresBefore 'Malformed add mutated Features.'
	}

	Invoke-Case 'update opaque bytes and reject a claimed multi-update counterpart' {
		$rows = @(
			New-Row 'Documents/Plans/A.md'
			New-Row 'Documents/Plans/B.md'
		)
		$fixture = New-FixtureRepository 'update' $rows @()
		$session = Add-Worktree $fixture 'session'
		$stagedA = "# Updated café`r`nline two`r`n"
		Set-Utf8File (Join-Path $session 'Temp/A.md') $stagedA
		$request = @{ schemaVersion=1; operation='update'; updates=@(@{
			plan='Documents/Plans/A.md'; expectedPlanSha256=(Get-Sha256 (Join-Path $session 'Documents/Plans/A.md')); expectedRowSha256=(Get-RowHash $fixture $session 'Documents/Plans/A.md'); stagedContent='Temp/A.md'
			tier='Medium'; effort=6; impact=4; risks=2; notes='updated | opaque'; dependsOn=@('Documents/Plans/B.md')
		}) }
		$requestPath = Write-Request $session 'update-a' $request
		$json = ConvertFrom-AgentJson (Invoke-WorktreeCli ((Get-OrderArguments $fixture 'update' $session) + @('--owner','update-owner','--session','update-session','--request',$requestPath)) 0)
		Assert-True ($json.handled -and $json.operation -ceq 'update') 'Update receipt is malformed.'
		Assert-Bytes (Join-Path $session 'Documents/Plans/A.md') $script:Utf8.GetBytes($stagedA) 'Opaque update bytes changed.'

		Invoke-RowClaim $fixture 'Documents/Plans/Order.md' 'B.md' 'counterpart-owner' 'counterpart-session' $session
		try {
			$plansBefore = [IO.File]::ReadAllBytes((Join-Path $session 'Documents/Plans/Order.md'))
			$aBefore = [IO.File]::ReadAllBytes((Join-Path $session 'Documents/Plans/A.md'))
			$bBefore = [IO.File]::ReadAllBytes((Join-Path $session 'Documents/Plans/B.md'))
			Set-Utf8File (Join-Path $session 'Temp/A2.md') "# A2`n"
			Set-Utf8File (Join-Path $session 'Temp/B2.md') "# B2`n"
			$blocked = @{ schemaVersion=1; operation='update'; updates=@(
				@{ plan='Documents/Plans/A.md'; expectedPlanSha256=(Get-Sha256 (Join-Path $session 'Documents/Plans/A.md')); expectedRowSha256=(Get-RowHash $fixture $session 'Documents/Plans/A.md'); stagedContent='Temp/A2.md'; tier='Small'; effort=2; impact=3; risks=1; notes='A2'; dependsOn=@() },
				@{ plan='Documents/Plans/B.md'; expectedPlanSha256=(Get-Sha256 (Join-Path $session 'Documents/Plans/B.md')); expectedRowSha256=(Get-RowHash $fixture $session 'Documents/Plans/B.md'); stagedContent='Temp/B2.md'; tier='Small'; effort=2; impact=3; risks=1; notes='B2'; dependsOn=@() }
			) }
			$blockedPath = Write-Request $session 'blocked-update' $blocked
			$conflict = ConvertFrom-AgentJson (Invoke-WorktreeCli ((Get-OrderArguments $fixture 'update' $session) + @('--owner','blocked-owner','--session','blocked-session','--request',$blockedPath)) 2)
			Assert-Equal $conflict.conflict 'claimed' 'Claimed counterpart did not report claimed conflict.'
			Assert-Bytes (Join-Path $session 'Documents/Plans/Order.md') $plansBefore 'Claimed counterpart changed Order.'
			Assert-Bytes (Join-Path $session 'Documents/Plans/A.md') $aBefore 'Claimed counterpart partially updated A.'
			Assert-Bytes (Join-Path $session 'Documents/Plans/B.md') $bBefore 'Claimed counterpart partially updated B.'
		}
		finally { Invoke-RowUnclaim $fixture 'Documents/Plans/Order.md' 'B.md' 'counterpart-owner' }
	}

	Invoke-Case 'update permits only the exact selected-plan claimant' {
		$fixture = New-FixtureRepository 'claimed-update' @(New-Row 'Documents/Plans/A.md') @()
		$session = Add-Worktree $fixture 'session'
		$peer = Add-Worktree $fixture 'peer'
		$claim = Invoke-ClaimNext $fixture $session 'amend-owner' 'plans' 0
		Assert-Equal $claim.Json.plan 'Documents/Plans/A.md' 'Claim selected the wrong plan for amendment.'

		$staged = "# Amended`n"
		Set-Utf8File (Join-Path $session 'Temp/A-amended.md') $staged
		$request = @{ schemaVersion=1; operation='update'; updates=@(@{
			plan='Documents/Plans/A.md'; expectedPlanSha256=(Get-Sha256 (Join-Path $session 'Documents/Plans/A.md')); expectedRowSha256=(Get-RowHash $fixture $session 'Documents/Plans/A.md'); stagedContent='Temp/A-amended.md'
			tier='Small'; effort=2; impact=3; risks=1; notes='amended by claimant'; dependsOn=@()
		}) }
		$requestPath = Write-Request $session 'claimed-update' $request
		$json = ConvertFrom-AgentJson (Invoke-WorktreeCli ((Get-OrderArguments $fixture 'update' $session) + @('--owner','amend-owner','--session','session-amend-owner','--request',$requestPath)) 0)
		Assert-True ($json.handled -and $json.operation -ceq 'update') 'Claimant update receipt is malformed.'
		Assert-Bytes (Join-Path $session 'Documents/Plans/A.md') $script:Utf8.GetBytes($staged) 'Claimant update did not replace the plan bytes.'
		$status = ConvertFrom-AgentJson (Invoke-WorktreeCli @('plan','row','status','--repo',$fixture.Common,'--order','Documents/Plans/Order.md','--plan','A.md','--owner','amend-owner') 0)
		Assert-True $status.ownedByRequester 'Claimant update released the selected row claim.'

		$sessionBefore = [IO.File]::ReadAllBytes((Join-Path $session 'Documents/Plans/A.md'))
		$wrongSession = Invoke-WorktreeCli ((Get-OrderArguments $fixture 'update' $session) + @('--owner','amend-owner','--session','other-session','--request',$requestPath)) 2
		Assert-Equal (ConvertFrom-AgentJson $wrongSession).conflict 'claimed' 'Different session bypassed the selected row claim.'
		Assert-Bytes (Join-Path $session 'Documents/Plans/A.md') $sessionBefore 'Different session changed claimant plan bytes.'

		Set-Utf8File (Join-Path $peer 'Temp/A-peer.md') "# Peer`n"
		$peerRequest = @{ schemaVersion=1; operation='update'; updates=@(@{
			plan='Documents/Plans/A.md'; expectedPlanSha256=(Get-Sha256 (Join-Path $peer 'Documents/Plans/A.md')); expectedRowSha256=(Get-RowHash $fixture $peer 'Documents/Plans/A.md'); stagedContent='Temp/A-peer.md'
			tier='Small'; effort=2; impact=3; risks=1; notes='peer'; dependsOn=@()
		}) }
		$peerRequestPath = Write-Request $peer 'claimed-update-peer' $peerRequest
		$wrongWorktree = Invoke-WorktreeCli ((Get-OrderArguments $fixture 'update' $peer) + @('--owner','amend-owner','--session','session-amend-owner','--request',$peerRequestPath)) 2
		Assert-Equal (ConvertFrom-AgentJson $wrongWorktree).conflict 'claimed' 'Different worktree bypassed the selected row claim.'
		Assert-True (Get-Validation $fixture $peer).Json.ok 'Different worktree update changed queue state.'
	}

	Invoke-Case 'claim concurrency, claimed prerequisite blocking, and owner mismatch' {
		$rows = @(
			New-Row 'Documents/Plans/A.md'
			New-Row 'Documents/Plans/B.md' 'Small' 2 3 1 @('Documents/Plans/A.md')
			New-Row 'Documents/Plans/C.md'
		)
		$fixture = New-FixtureRepository 'claim-concurrency' $rows @()
		$one = Add-Worktree $fixture 'one'
		$two = Add-Worktree $fixture 'two'
		$a = Invoke-ClaimNext $fixture $one 'owner-a' 'plans' 0
		Assert-Equal $a.Json.plan 'Documents/Plans/A.md' 'First claim selected the wrong row.'
		$blocked = Invoke-ClaimNext $fixture $two 'owner-b' 'plans' 2 'Documents/Plans/B.md'
		Assert-Equal $blocked.Json.reason 'explicit-plan-blocked-or-missing' 'Dependent explicit claim reported the wrong reason.'
		Assert-True (@($blocked.Json.blockers[0].dependencies) -ccontains 'Documents/Plans/A.md') 'Dependent blocker omitted its prerequisite.'
		$c = Invoke-ClaimNext $fixture $two 'owner-c' 'plans' 0
		Assert-Equal $c.Json.plan 'Documents/Plans/C.md' 'Automatic claim did not skip blocked rows.'
		$plansBefore = [IO.File]::ReadAllBytes((Join-Path $one 'Documents/Plans/Order.md'))
		$mismatch = ConvertFrom-AgentJson (Invoke-WorktreeCli ((Get-OrderArguments $fixture 'complete' $one) + @('--owner','wrong-owner','--session','wrong-session','--plan','Documents/Plans/A.md')) 2)
		Assert-Equal $mismatch.conflict 'owner-mismatch' 'Complete owner mismatch was not reported.'
		Assert-Bytes (Join-Path $one 'Documents/Plans/Order.md') $plansBefore 'Owner mismatch mutated Order.'
		Invoke-RowUnclaim $fixture 'Documents/Plans/Order.md' 'A.md' 'owner-a'
		Invoke-RowUnclaim $fixture 'Documents/Plans/Order.md' 'C.md' 'owner-c'
	}

	Invoke-Case 'complete prunes edges, reapplies idempotently, and rolls back exact bytes' {
		$rows = @(
			New-Row 'Documents/Plans/A.md'
			New-Row 'Documents/Plans/B.md' 'Small' 2 3 1 @('Documents/Plans/A.md')
		)
		$fixture = New-FixtureRepository 'complete' $rows @()
		$session = Add-Worktree $fixture 'session'
		Invoke-RowClaim $fixture 'Documents/Plans/Order.md' 'A.md' 'complete-owner' 'complete-session' $session
		$result = ConvertFrom-AgentJson (Invoke-WorktreeCli ((Get-OrderArguments $fixture 'complete' $session) + @('--owner','complete-owner','--session','complete-session','--plan','Documents/Plans/A.md')) 0)
		Assert-True ($result.handled -and -not $result.reapply) 'Complete receipt is malformed.'
		Assert-True (-not (Test-Path -LiteralPath (Join-Path $session 'Documents/Plans/A.md'))) 'Complete retained the plan file.'
		$expected = Get-OrderText 'Plans' @(New-Row 'Documents/Plans/B.md') "`n" 'Reference.md'
		Assert-Bytes (Join-Path $session 'Documents/Plans/Order.md') $script:Utf8.GetBytes($expected) 'Complete did not prune exact row/edge bytes.'
		$beforeReapply = [IO.File]::ReadAllBytes((Join-Path $session 'Documents/Plans/Order.md'))
		$reapply = ConvertFrom-AgentJson (Invoke-WorktreeCli ((Get-OrderArguments $fixture 'complete' $session) + @('--owner','complete-owner','--session','complete-session','--plan','Documents/Plans/A.md','--reapply')) 0)
		Assert-True $reapply.reapply 'Reapply receipt did not identify reapply.'
		Assert-Bytes (Join-Path $session 'Documents/Plans/Order.md') $beforeReapply 'Reapply was not byte-idempotent.'
		Invoke-RowUnclaim $fixture 'Documents/Plans/Order.md' 'A.md' 'complete-owner'

		$rollbackFixture = New-FixtureRepository 'complete-rollback' $rows @()
		$rollbackSession = Add-Worktree $rollbackFixture 'session'
		Invoke-RowClaim $rollbackFixture 'Documents/Plans/Order.md' 'A.md' 'rollback-owner' 'rollback-session' $rollbackSession
		$plansBefore = [IO.File]::ReadAllBytes((Join-Path $rollbackSession 'Documents/Plans/Order.md'))
		$featuresPath = Join-Path $rollbackSession 'Documents/Features/Order.md'
		$featuresBefore = [IO.File]::ReadAllBytes($featuresPath)
		$planBefore = [IO.File]::ReadAllBytes((Join-Path $rollbackSession 'Documents/Plans/A.md'))
		[IO.File]::SetAttributes($featuresPath, [IO.FileAttributes]::ReadOnly)
		try { Invoke-WorktreeCli ((Get-OrderArguments $rollbackFixture 'complete' $rollbackSession) + @('--owner','rollback-owner','--session','rollback-session','--plan','Documents/Plans/A.md')) 1 | Out-Null }
		finally { [IO.File]::SetAttributes($featuresPath, [IO.FileAttributes]::Normal) }
		Assert-Bytes (Join-Path $rollbackSession 'Documents/Plans/Order.md') $plansBefore 'Rollback did not restore Plans bytes.'
		Assert-Bytes $featuresPath $featuresBefore 'Rollback changed Features bytes.'
		Assert-Bytes (Join-Path $rollbackSession 'Documents/Plans/A.md') $planBefore 'Rollback deleted the plan file.'
		Invoke-RowUnclaim $rollbackFixture 'Documents/Plans/Order.md' 'A.md' 'rollback-owner'
	}

	Invoke-Case 'stale session cannot reclaim after completion lands' {
		$fixture = New-FixtureRepository -Name 'stale' -PlansRows @((New-Row 'Documents/Plans/A.md'), (New-Row 'Documents/Plans/B.md')) -FeaturesRows @()
		$worker = Add-Worktree $fixture 'worker'
		$stale = Add-Worktree $fixture 'stale'
		$a = Invoke-ClaimNext $fixture $worker 'landing-owner' 'plans' 0 'Documents/Plans/A.md'
		Assert-True $a.Json.claimed 'Worker did not claim A.'
		$preLanding = Invoke-ClaimNext $fixture $stale 'prelanding-owner' 'plans' 2 'Documents/Plans/A.md'
		Assert-Equal $preLanding.Json.reason 'explicit-plan-blocked-or-missing' 'Pre-landing peer did not observe the live claim.'
		Invoke-WorktreeCli ((Get-OrderArguments $fixture 'complete' $worker) + @('--owner','landing-owner','--session','landing-session','--plan','Documents/Plans/A.md')) 0 | Out-Null
		Invoke-Git $worker @('add', '--all') | Out-Null
		Invoke-Git $worker @('commit', '-m', 'complete A') | Out-Null
		$completedCommit = Invoke-Git $worker @('rev-parse', 'HEAD')
		Invoke-Git $fixture.Primary @('merge', '--ff-only', $completedCommit) | Out-Null
		Invoke-RowUnclaim $fixture 'Documents/Plans/Order.md' 'A.md' 'landing-owner'
		$staleResult = Invoke-ClaimNext $fixture $stale 'stale-owner' 'plans' 2
		Assert-Equal $staleResult.Json.reason 'stale-session' 'Old worktree was not rejected as stale.'
		$current = Add-Worktree $fixture 'current'
		$currentResult = Invoke-ClaimNext $fixture $current 'current-owner' 'plans' 0
		Assert-Equal $currentResult.Json.plan 'Documents/Plans/B.md' 'Current worktree did not select remaining plan.'
		Invoke-RowUnclaim $fixture 'Documents/Plans/Order.md' 'B.md' 'current-owner'
		$missing = Invoke-ClaimNext $fixture $current 'missing-owner' 'plans' 2 'Documents/Plans/A.md'
		Assert-Equal $missing.Json.reason 'explicit-plan-blocked-or-missing' 'Completed plan was resurrected.'
	}

	Invoke-Case 'finalize landing holds landing then queues across primary advance and unwinds failures' {
		foreach ($injection in @('pre-advance', 'post-advance')) {
			$fixture = New-FixtureRepository "finalize-$injection" @(New-Row 'Documents/Plans/A.md') @()
			$advance = Add-Worktree $fixture 'advance'
			Set-Utf8File (Join-Path $advance 'Documents/Plans/B.md') "# B`n"
			Set-Utf8File (Join-Path $advance 'Documents/Plans/Order.md') (Get-OrderText 'Plans' @((New-Row 'Documents/Plans/A.md'), (New-Row 'Documents/Plans/B.md')) "`n" 'Reference.md')
			Invoke-Git $advance @('add', '--all') | Out-Null
			Invoke-Git $advance @('commit', '-m', 'queue-changing advance') | Out-Null
			$verifiedCommit = Invoke-Git $advance @('rev-parse', 'HEAD')
			$baselineCommit = Invoke-Git $fixture.Primary @('rev-parse', 'HEAD')
			$observer = Add-Worktree $fixture 'observer'
			$owner = "finalize-$injection-owner"
			$session = "finalize-$injection-session"
			$events = [Collections.Generic.List[string]]::new()
			$acquiredQueues = [Collections.Generic.List[string]]::new()
			$landingHeld = $false
			$caughtInjection = $false
			try {
				Invoke-WorktreeCli @('lock','claim','--repo',$fixture.Common,'--owner',$owner,'--session',$session,'--worktree',$advance,'--lease-seconds','3600') 0 | Out-Null
				$landingHeld = $true
				$events.Add('claim-landing')
				$landingStatus = ConvertFrom-AgentJson (Invoke-WorktreeCli @('lock','status','--repo',$fixture.Common) 0)
				Assert-Equal $landingStatus.owner $owner 'Landing lock owner mismatch before queue acquisition.'

				foreach ($order in @('Documents/Features/Order.md', 'Documents/Plans/Order.md')) {
					Invoke-QueueLock $fixture $order $owner $session $advance | Out-Null
					$acquiredQueues.Add($order)
					$events.Add("claim-$order")
					$status = ConvertFrom-AgentJson (Invoke-WorktreeCli @('plan','queue','status','--repo',$fixture.Common,'--order',$order,'--owner',$owner) 0)
					Assert-True ($status.owner -ceq $owner -and $status.ownedByRequester) "Queue '$order' was not owned after acquisition."
				}

				$claimArguments = @('plan','order','claim-next','--repo',$fixture.Common,'--primary-worktree',$fixture.Primary,'--worktree',$observer,'--branch',$fixture.Branch,'--owner',"observer-$injection",'--session',"observer-$injection",'--queue','plans')
				$blockedBefore = ConvertFrom-AgentJson (Invoke-WorktreeCli $claimArguments 2)
				Assert-Equal $blockedBefore.reason 'already-locked' 'Concurrent claim-next was not excluded before primary ref advance.'
				$events.Add('claim-next-blocked-before')
				if ($injection -ceq 'pre-advance') { throw 'injected pre-advance failure' }

				Invoke-Git $fixture.Primary @('rebase', $verifiedCommit) | Out-Null
				$events.Add('advance-primary')
				$staleAfter = ConvertFrom-AgentJson (Invoke-WorktreeCli $claimArguments 2)
				Assert-Equal $staleAfter.reason 'stale-session' 'Post-advance observer was not rejected as stale.'
				$events.Add('claim-next-stale-after')
				throw 'injected post-advance failure'
			}
			catch {
				Assert-Equal $_.Exception.Message "injected $injection failure" 'Finalize fixture failed for a reason other than the injected failure.'
				$caughtInjection = $true
			}
			finally {
				for ($index = $acquiredQueues.Count - 1; $index -ge 0; --$index) {
					$order = $acquiredQueues[$index]
					$status = ConvertFrom-AgentJson (Invoke-WorktreeCli @('plan','queue','status','--repo',$fixture.Common,'--order',$order,'--owner',$owner) 0)
					Assert-True ($status.owner -ceq $owner -and $status.ownedByRequester) "Queue '$order' changed owner before safe-stop."
					Invoke-QueueUnlock $fixture $order $owner
					$events.Add("release-$order")
				}
				if ($landingHeld) {
					$landingStatus = ConvertFrom-AgentJson (Invoke-WorktreeCli @('lock','status','--repo',$fixture.Common) 0)
					Assert-Equal $landingStatus.owner $owner 'Landing lock changed owner before safe-stop.'
					Invoke-WorktreeCli @('lock','release','--repo',$fixture.Common,'--owner',$owner) 0 | Out-Null
					$events.Add('release-landing')
				}
			}

			Assert-True $caughtInjection "The $injection injection was not exercised."
			$expectedEvents = if ($injection -ceq 'pre-advance') {
				@('claim-landing','claim-Documents/Features/Order.md','claim-Documents/Plans/Order.md','claim-next-blocked-before','release-Documents/Plans/Order.md','release-Documents/Features/Order.md','release-landing')
			} else {
				@('claim-landing','claim-Documents/Features/Order.md','claim-Documents/Plans/Order.md','claim-next-blocked-before','advance-primary','claim-next-stale-after','release-Documents/Plans/Order.md','release-Documents/Features/Order.md','release-landing')
			}
			Assert-Equal ($events -join '|') ($expectedEvents -join '|') "The $injection lock/ref event order was incorrect."
			foreach ($order in @('Documents/Features/Order.md', 'Documents/Plans/Order.md')) {
				$status = ConvertFrom-AgentJson (Invoke-WorktreeCli @('plan','queue','status','--repo',$fixture.Common,'--order',$order) 2)
				Assert-True (-not $status.held) "The $injection safe-stop retained queue '$order'."
			}
			$landingStatus = ConvertFrom-AgentJson (Invoke-WorktreeCli @('lock','status','--repo',$fixture.Common) 2)
			Assert-True (-not $landingStatus.held) "The $injection safe-stop retained the landing lock."
			$expectedPrimary = if ($injection -ceq 'pre-advance') { $baselineCommit } else { $verifiedCommit }
			Assert-Equal (Invoke-Git $fixture.Primary @('rev-parse', 'HEAD')) $expectedPrimary "The $injection primary ref result was incorrect."

			if ($injection -ceq 'post-advance') {
				$current = Add-Worktree $fixture 'current'
				$currentClaim = Invoke-ClaimNext $fixture $current 'post-advance-current-owner' 'plans' 0
				Assert-Equal $currentClaim.Json.plan 'Documents/Plans/A.md' 'Post-advance claim-next did not return the public canonical plan identity.'
				Invoke-RowUnclaim $fixture 'Documents/Plans/Order.md' 'A.md' 'post-advance-current-owner'
			}
		}
	}

	Invoke-Case 'queue-lock contention fails without repository mutation' {
		$fixture = New-FixtureRepository 'contention' @(New-Row 'Documents/Plans/A.md') @()
		$session = Add-Worktree $fixture 'session'
		$plansBefore = [IO.File]::ReadAllBytes((Join-Path $session 'Documents/Plans/Order.md'))
		$featuresBefore = [IO.File]::ReadAllBytes((Join-Path $session 'Documents/Features/Order.md'))
		Invoke-QueueLock $fixture 'Documents/Plans/Order.md' 'blocker-owner' 'blocker-session' $session | Out-Null
		try {
			$arguments = @('plan', 'order', 'claim-next', '--repo', $fixture.Common, '--primary-worktree', $fixture.Primary, '--worktree', $session, '--branch', $fixture.Branch, '--owner', 'contended-claim', '--session', 'contended-session', '--queue', 'plans')
			$claim = ConvertFrom-AgentJson (Invoke-WorktreeCli $arguments 2)
			Assert-Equal $claim.reason 'already-locked' 'Contended claim did not report the held queue lock.'
			$featuresStatus = ConvertFrom-AgentJson (Invoke-WorktreeCli @('plan','queue','status','--repo',$fixture.Common,'--order','Documents/Features/Order.md') 2)
			Assert-True (-not $featuresStatus.held) 'Partial acquisition failure retained the earlier Features queue lock.'
		}
		finally { Invoke-QueueUnlock $fixture 'Documents/Plans/Order.md' 'blocker-owner' }
		Assert-Bytes (Join-Path $session 'Documents/Plans/Order.md') $plansBefore 'Contention mutated Plans.'
		Assert-Bytes (Join-Path $session 'Documents/Features/Order.md') $featuresBefore 'Contention mutated Features.'
	}
}
finally {
	Clear-TrackedQueueLocks
	for ($index = $script:Claims.Count - 1; $index -ge 0; --$index) {
		$claim = $script:Claims[$index]
		try {
			Invoke-Process -FilePath $script:Executable -Arguments @('plan', 'row', 'unclaim', '--repo', $claim.Fixture.Common, '--order', $claim.Order, '--plan', $claim.Plan, '--owner', $claim.Owner) | Out-Null
		}
		catch {
			Write-Warning "Could not release tracked row claim '$($claim.Plan)' for '$($claim.Owner)': $($_.Exception.Message)"
		}
	}
	if (Test-Path -LiteralPath $script:FixtureRoot) {
		Get-ChildItem -LiteralPath $script:FixtureRoot -Recurse -Force -ErrorAction SilentlyContinue | ForEach-Object {
			if (-not $_.PSIsContainer -and ($_.Attributes -band [IO.FileAttributes]::ReadOnly)) { $_.Attributes = [IO.FileAttributes]::Normal }
		}
		Remove-Item -LiteralPath $script:FixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
	}
}

$summary = [ordered]@{
	passed = $script:Passed.Count
	failed = $script:Failures.Count
	cases = @($script:Passed)
	limitations = @(
		'Abrupt termination, machine failure, and power loss are intentionally not exercised or guaranteed.'
	)
}
if ($script:Failures.Count -ne 0) {
	$summary.failures = @($script:Failures)
	$summary | ConvertTo-Json -Depth 10
	throw "$($script:Failures.Count) WorktreeCli plan-order fixture case(s) failed."
}
$summary | ConvertTo-Json -Depth 10

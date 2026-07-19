[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $Executable
)

$ErrorActionPreference = 'Stop'

$script:Executable = (Get-Item -LiteralPath $Executable -Force -ErrorAction Stop).FullName
$script:RepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$script:FixtureRoot = Join-Path $script:RepositoryRoot "Temp/WorktreeCliPlanOrderFixtures/$([Guid]::NewGuid().ToString('N'))"
# The plan queue tables live in the machine-local store under %LOCALAPPDATA%\BrokenEngineLocks;
# each fixture repo gets its own scratch LOCALAPPDATA so its queue store, queue locks, and row
# claims are isolated and cleaned with the fixture tree. Restored in the finally block.
$script:OriginalLocalAppData = $env:LOCALAPPDATA
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
	[hashtable] $PlanContents = @{},
	[switch] $NoOrderFiles
) {
	$primary = Join-Path $script:FixtureRoot "$Name/primary"
	[IO.Directory]::CreateDirectory($primary) | Out-Null
	Invoke-Git $primary @('init', '--initial-branch=main', '.') | Out-Null
	Invoke-Git $primary @('config', 'user.email', 'worktreecli-fixture@example.invalid') | Out-Null
	Invoke-Git $primary @('config', 'user.name', 'WorktreeCli Fixture') | Out-Null
	Invoke-Git $primary @('config', 'core.autocrlf', 'false') | Out-Null
	Invoke-Git $primary @('config', 'core.eol', 'lf') | Out-Null
	# Deep session worktrees push fixture git operations past MAX_PATH; git rebase stats its '<sha>...<sha>' range as a path.
	Invoke-Git $primary @('config', 'core.longpaths', 'true') | Out-Null
	if (-not $NoOrderFiles) {
		Set-Utf8File (Join-Path $primary 'Documents/Plans/Order.md') (Get-OrderText 'Plans' $PlansRows $PlansNewline 'Reference.md')
		Set-Utf8File (Join-Path $primary 'Documents/Features/Order.md') (Get-OrderText 'Features' $FeaturesRows $FeaturesNewline 'Reference.txt')
		Set-Utf8File (Join-Path $primary 'Documents/Plans/Reference.md') "# Plans reference$PlansNewline"
		Set-Utf8File (Join-Path $primary 'Documents/Features/Reference.txt') "Features reference$FeaturesNewline"
	}
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
	# Isolate the machine-local queue store per fixture, then seed it from the tracked Order.md files.
	$localAppData = Join-Path (Join-Path $script:FixtureRoot $Name) 'localappdata'
	[IO.Directory]::CreateDirectory($localAppData) | Out-Null
	$env:LOCALAPPDATA = $localAppData
	$fixture = [pscustomobject]@{ Primary = $primary; Common = [IO.Path]::GetFullPath($common); Branch = 'main'; Name = $Name; LocalAppData = $localAppData }
	Invoke-WorktreeCli @('plan', 'order', 'init', '--repo', $fixture.Common, '--worktree', $primary) 0 | Out-Null
	return $fixture
}

function Get-FixtureStoreDirectory($Fixture) {
	$root = Join-Path $Fixture.LocalAppData 'BrokenEngineLocks/plan-queue-state'
	$directories = @(Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue)
	Assert-Equal $directories.Count 1 "Expected exactly one plan-queue-state repo directory for fixture '$($Fixture.Name)'."
	return $directories[0].FullName
}

function Get-StoreOrderPath($Fixture, [string] $Queue) {
	$fileName = if ($Queue -ceq 'Plans') { 'Plans-Order.md' } else { 'Features-Order.md' }
	return Join-Path (Get-FixtureStoreDirectory $Fixture) $fileName
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

$executableItem = Get-Item -LiteralPath $script:Executable -Force
if ($executableItem.PSIsContainer -or ($executableItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $executableItem.Length -eq 0) {
	throw "WorktreeCli executable must be a nonempty ordinary file: '$script:Executable'."
}
$help = Invoke-Process -FilePath $script:Executable -Arguments @('--help')
if ($help.ExitCode -ne 0) { throw "WorktreeCli --help exited $($help.ExitCode)." }
Assert-True $help.Stdout.Contains('WorktreeCli.exe plan order validate', [StringComparison]::Ordinal) 'WorktreeCli does not advertise plan order; build it before running this fixture.'
Assert-True $help.Stdout.Contains('WorktreeCli.exe plan order init', [StringComparison]::Ordinal) 'WorktreeCli does not advertise plan order init; build the machine-local queue store binary before running this fixture.'

[IO.Directory]::CreateDirectory($script:FixtureRoot) | Out-Null
try {
	Invoke-Case 'init seeds the machine-local store from tracked Order.md files' {
		$plans = @(
			New-Row 'Documents/Plans/Zeta.md' 'Small' 10 1 1 @() 'approximate order | is allowed'
			New-Row 'Documents/Plans/Alpha.md' 'Small' 2 3 1 @('Documents/Features/Foundation.txt') 'depends cross-queue'
		)
		$features = @(New-Row 'Documents/Features/Foundation.txt' 'Architectural' 5 4 3 @() 'naive cafe')
		$fixture = New-FixtureRepository 'init-seed' $plans $features "`r`n" "`n" @{ 'Documents/Plans/Alpha.md' = "# Café 漢字`r`n" }
		$treePlans = [IO.File]::ReadAllBytes((Join-Path $fixture.Primary 'Documents/Plans/Order.md'))
		$treeFeatures = [IO.File]::ReadAllBytes((Join-Path $fixture.Primary 'Documents/Features/Order.md'))
		Assert-Bytes (Get-StoreOrderPath $fixture 'Plans') $treePlans 'init did not seed the Plans store byte-for-byte from the tracked Order.md.'
		Assert-Bytes (Get-StoreOrderPath $fixture 'Features') $treeFeatures 'init did not seed the Features store byte-for-byte from the tracked Order.md.'
		# init reads the store, not the tree; validate resolves rows from the store and plan files under the worktree.
		$validation = Get-Validation $fixture $fixture.Primary
		Assert-True $validation.Json.ok 'Seeded store did not validate.'
		Assert-Equal @($validation.Json.rows).Count 3 'Seeded store row count differs.'
		# Idempotent re-init keeps an existing non-empty store without --force: exit 0, alreadyInitialized, no mutation.
		$reinit = Invoke-Process -FilePath $script:Executable -Arguments @('plan', 'order', 'init', '--repo', $fixture.Common, '--worktree', $fixture.Primary)
		Assert-Equal $reinit.ExitCode 0 'Idempotent re-init of a non-empty store should exit 0.'
		Assert-True (($reinit.Stdout | ConvertFrom-Json).alreadyInitialized) 'Idempotent re-init did not report alreadyInitialized.'
		Assert-Bytes (Get-StoreOrderPath $fixture 'Plans') $treePlans 'Idempotent re-init mutated the Plans store.'
	}

	Invoke-Case 'init on an empty repository writes an empty-template store that validates' {
		$fixture = New-FixtureRepository -Name 'init-empty' -NoOrderFiles
		Assert-True (Test-Path -LiteralPath (Get-StoreOrderPath $fixture 'Plans')) 'init did not create an empty Plans store.'
		Assert-True (Test-Path -LiteralPath (Get-StoreOrderPath $fixture 'Features')) 'init did not create an empty Features store.'
		$validation = Get-Validation $fixture $fixture.Primary
		Assert-True $validation.Json.ok 'Empty-template store did not validate.'
		Assert-Equal @($validation.Json.rows).Count 0 'Empty-template store reported rows.'
	}

	Invoke-Case 'validate returns ok with an orphan-plan reported as a non-blocking notice' {
		$fixture = New-FixtureRepository 'orphan-notice' @((New-Row 'Documents/Plans/A.md')) @()
		Set-Utf8File (Join-Path $fixture.Primary 'Documents/Plans/Orphan.md') '# orphan'
		$first = Get-Validation $fixture $fixture.Primary 0
		$second = Get-Validation $fixture $fixture.Primary 0
		Assert-Equal $first.Result.Stdout $second.Result.Stdout 'Orphan-notice validate was not byte-deterministic.'
		Assert-True $first.Json.ok 'Orphan-only validate did not report ok:true.'
		Assert-Equal @($first.Json.diagnostics).Count 0 'Orphan plan produced a blocking diagnostic.'
		Assert-True (@($first.Json.notices.code) -ccontains 'orphan-plan') "Orphan plan was not reported as a notice; got '$(@($first.Json.notices.code) -join ', ')'."
	}

	Invoke-Case 'add sequences publish to the store and re-add is retry-idempotent' {
		$fixture = New-FixtureRepository 'add-idempotent' @() @((New-Row 'Documents/Features/Live.txt'))
		# Migration mirror: the queue tables are machine-local post-init, so drop the tracked Order.md files from the
		# tree before branching the session — production sessions never carry an in-tree Order.md.
		Invoke-Git $fixture.Primary @('rm', '--quiet', '--', 'Documents/Plans/Order.md', 'Documents/Features/Order.md') | Out-Null
		Invoke-Git $fixture.Primary @('commit', '-m', 'migrate order files out of the tree') | Out-Null
		$session = Add-Worktree $fixture 'session'
		foreach ($entry in @(
			@('Documents/Plans/First.md', "# First café`r`n"),
			@('Documents/Features/Second.txt', "Second 漢字`n"),
			@('Documents/Plans/Third.md', "# Third`n")
		)) { Set-Utf8File (Join-Path $session $entry[0]) $entry[1] }
		# ConvertTo-Json collapses a single-element outer array, so force the one sequence to
		# serialize as sequences:[[...]] with the unary array operator.
		$request = @{
			schemaVersion = 1; operation = 'add'; sequences = ,@(
				@{ queue='plans'; plan='Documents/Plans/First.md'; tier='Small'; effort=4; impact=3; risks=1; notes='first | pipe'; dependsOn=@('Documents/Features/Live.txt') },
				@{ queue='features'; plan='Documents/Features/Second.txt'; tier='Medium'; effort=5; impact=3; risks=2; notes='second'; dependsOn=@() },
				@{ queue='plans'; plan='Documents/Plans/Third.md'; tier='Large'; effort=7; impact=4; risks=2; notes='third'; dependsOn=@() }
			)
		}
		$requestPath = Write-Request $session 'add' $request
		$addArguments = (Get-OrderArguments $fixture 'add' $session) + @('--owner','add-owner','--session','add-session','--request',$requestPath)
		$json = ConvertFrom-AgentJson (Invoke-WorktreeCli $addArguments 0)
		Assert-True ($json.handled -and $json.operation -ceq 'add') 'Add receipt is malformed.'
		# add writes the machine-local store, never the worktree tree.
		Assert-True (-not (Test-Path -LiteralPath (Join-Path $session 'Documents/Plans/Order.md'))) 'Add created an in-tree Order.md.'
		$afterAdd = Get-Validation $fixture $session
		Assert-True $afterAdd.Json.ok 'Added graph did not validate.'
		Assert-Equal @($afterAdd.Json.rows | Where-Object plan -CEQ 'Documents/Plans/First.md').Count 1 'Add did not publish the First row.'

		# Identical re-add of the same request is skipped: it succeeds and does not duplicate rows.
		$storeBefore = [IO.File]::ReadAllBytes((Get-StoreOrderPath $fixture 'Plans'))
		Invoke-WorktreeCli $addArguments 0 | Out-Null
		Assert-Bytes (Get-StoreOrderPath $fixture 'Plans') $storeBefore 'Idempotent re-add mutated the Plans store.'
		$afterReadd = Get-Validation $fixture $session
		Assert-Equal @($afterReadd.Json.rows | Where-Object plan -CEQ 'Documents/Plans/First.md').Count 1 'Idempotent re-add duplicated the First row.'

		# A mismatched duplicate (same plan identity, different row data) is rejected without mutation.
		$mismatch = @{ schemaVersion = 1; operation = 'add'; sequences = ,@(@{ queue='plans'; plan='Documents/Plans/First.md'; tier='Large'; effort=9; impact=1; risks=3; notes='mismatched'; dependsOn=@() }) }
		$mismatchPath = Write-Request $session 'add-mismatch' $mismatch
		$mismatchResult = Invoke-Process -FilePath $script:Executable -Arguments ((Get-OrderArguments $fixture 'add' $session) + @('--owner','mismatch-owner','--session','mismatch-session','--request',$mismatchPath))
		Assert-True ($mismatchResult.ExitCode -ne 0) 'A mismatched duplicate add did not error.'
		Assert-Bytes (Get-StoreOrderPath $fixture 'Plans') $storeBefore 'Mismatched duplicate add mutated the Plans store.'
	}

	Invoke-Case 'update replaces plan bytes and the store row, and rejects a claimed target with zero mutation' {
		$rows = @(
			New-Row 'Documents/Plans/A.md'
			New-Row 'Documents/Plans/B.md'
		)
		$fixture = New-FixtureRepository 'update' $rows @()
		$session = Add-Worktree $fixture 'session'

		# Successful update: replace A's plan bytes and its store row from a Temp/-staged replacement.
		$staged = "# Updated café`r`nline two`r`n"
		Set-Utf8File (Join-Path $session 'Temp/A.md') $staged
		$rowHashBefore = Get-RowHash $fixture $session 'Documents/Plans/A.md'
		$request = @{ schemaVersion=1; operation='update'; updates=@(@{
			plan='Documents/Plans/A.md'; expectedPlanSha256=(Get-Sha256 (Join-Path $session 'Documents/Plans/A.md')); expectedRowSha256=$rowHashBefore; stagedContent='Temp/A.md'
			tier='Medium'; effort=6; impact=4; risks=2; notes='updated | opaque'; dependsOn=@('Documents/Plans/B.md')
		}) }
		$requestPath = Write-Request $session 'update-a' $request
		$json = ConvertFrom-AgentJson (Invoke-WorktreeCli ((Get-OrderArguments $fixture 'update' $session) + @('--owner','update-owner','--session','update-session','--request',$requestPath)) 0)
		Assert-True ($json.handled -and $json.operation -ceq 'update') 'Update receipt is malformed.'
		Assert-Bytes (Join-Path $session 'Documents/Plans/A.md') $script:Utf8.GetBytes($staged) 'Update did not replace the worktree plan bytes.'
		Assert-True ((Get-RowHash $fixture $session 'Documents/Plans/A.md') -cne $rowHashBefore) 'Update did not persist the new row to the store.'

		# A target row claimed by a peer is rejected without mutating the store or the plan bytes.
		Invoke-RowClaim $fixture 'Documents/Plans/Order.md' 'B.md' 'counterpart-owner' 'counterpart-session' $session
		try {
			$storeBefore = [IO.File]::ReadAllBytes((Get-StoreOrderPath $fixture 'Plans'))
			$bBefore = [IO.File]::ReadAllBytes((Join-Path $session 'Documents/Plans/B.md'))
			Set-Utf8File (Join-Path $session 'Temp/B.md') "# B updated`n"
			$blocked = @{ schemaVersion=1; operation='update'; updates=@(@{
				plan='Documents/Plans/B.md'; expectedPlanSha256=(Get-Sha256 (Join-Path $session 'Documents/Plans/B.md')); expectedRowSha256=(Get-RowHash $fixture $session 'Documents/Plans/B.md'); stagedContent='Temp/B.md'
				tier='Small'; effort=2; impact=3; risks=1; notes='B'; dependsOn=@()
			}) }
			$blockedPath = Write-Request $session 'update-b-blocked' $blocked
			$conflict = ConvertFrom-AgentJson (Invoke-WorktreeCli ((Get-OrderArguments $fixture 'update' $session) + @('--owner','blocked-owner','--session','blocked-session','--request',$blockedPath)) 2)
			Assert-Equal $conflict.conflict 'claimed' 'Claimed target did not report a claimed conflict.'
			Assert-Bytes (Get-StoreOrderPath $fixture 'Plans') $storeBefore 'Claimed-target update mutated the store.'
			Assert-Bytes (Join-Path $session 'Documents/Plans/B.md') $bBefore 'Claimed-target update mutated the plan bytes.'
		}
		finally { Invoke-RowUnclaim $fixture 'Documents/Plans/Order.md' 'B.md' 'counterpart-owner' }
	}

	Invoke-Case 'claim-next succeeds with a dirty session worktree' {
		$fixture = New-FixtureRepository 'claim-dirty' @((New-Row 'Documents/Plans/A.md')) @()
		$session = Add-Worktree $fixture 'session'
		# Dirty the tree with unrelated changes; the queue is machine-local, so the clean-tree gate is gone.
		Set-Utf8File (Join-Path $session 'Dirty.txt') "unstaged untracked change`n"
		Set-Utf8File (Join-Path $session 'Documents/Plans/Reference.md') "# changed reference`n"
		$claim = Invoke-ClaimNext $fixture $session 'dirty-owner' 'plans' 0
		Assert-Equal $claim.Json.plan 'Documents/Plans/A.md' 'Dirty-tree claim selected the wrong plan.'
		Assert-True $claim.Json.claimed 'Dirty-tree claim did not create a row claim.'
		Invoke-RowUnclaim $fixture 'Documents/Plans/Order.md' 'A.md' 'dirty-owner'
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
		$storeBefore = [IO.File]::ReadAllBytes((Get-StoreOrderPath $fixture 'Plans'))
		$mismatch = ConvertFrom-AgentJson (Invoke-WorktreeCli ((Get-OrderArguments $fixture 'complete' $one) + @('--owner','wrong-owner','--session','wrong-session','--plan','Documents/Plans/A.md')) 2)
		Assert-Equal $mismatch.conflict 'owner-mismatch' 'Complete owner mismatch was not reported.'
		Assert-Bytes (Get-StoreOrderPath $fixture 'Plans') $storeBefore 'Owner mismatch mutated the store.'
		Invoke-RowUnclaim $fixture 'Documents/Plans/Order.md' 'A.md' 'owner-a'
		Invoke-RowUnclaim $fixture 'Documents/Plans/Order.md' 'C.md' 'owner-c'
	}

	Invoke-Case 'corrupt row claim is skipped with a diagnostic, still blocks its row, and recovers by deletion' {
		$rows = @(
			New-Row 'Documents/Plans/A.md'
			New-Row 'Documents/Plans/B.md' 'Small' 2 2 1
		)
		$fixture = New-FixtureRepository 'corrupt-claim' $rows @()
		$session = Add-Worktree $fixture 'session'
		$claim = Invoke-ClaimNext $fixture $session 'corrupt-owner' 'plans' 0
		Assert-Equal $claim.Json.plan 'Documents/Plans/A.md' 'Setup claim selected the wrong row.'
		$keyParts = ([string] $claim.Json.logicalKey) -split "`n"
		Assert-Equal $keyParts.Count 3 'Claim logicalKey did not contain repository, order, and plan segments.'
		$queueHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($script:Utf8.GetBytes($keyParts[0..1] -join "`n"))).ToLowerInvariant()
		$rowHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($script:Utf8.GetBytes($keyParts -join "`n"))).ToLowerInvariant()
		$claimPath = Join-Path $env:LOCALAPPDATA "BrokenEngineLocks\plan-row\$queueHash\$rowHash.lock"
		Assert-True (Test-Path -LiteralPath $claimPath) 'Computed row claim path does not exist.'
		[IO.File]::SetAttributes($claimPath, [IO.FileAttributes]::Normal)
		Set-RawFile $claimPath ([byte[]](0xff, 0x7b, 0x67, 0x61, 0x72, 0x62))
		$b = Invoke-ClaimNext $fixture $session 'skip-owner' 'plans' 0
		Assert-Equal $b.Json.plan 'Documents/Plans/B.md' 'Snapshot validation did not skip the corrupt claim.'
		Assert-True ($b.Result.Stderr.IndexOf($claimPath, [StringComparison]::OrdinalIgnoreCase) -ge 0) 'Skip diagnostic did not name the corrupt claim path.'
		$blocked = Invoke-ClaimNext $fixture $session 'retry-owner' 'plans' 2 'Documents/Plans/A.md'
		Assert-Equal $blocked.Json.reason 'explicit-plan-blocked-or-missing' 'Corrupt claim did not block its own row.'
		Assert-True ([bool] $blocked.Json.blockers[0].claimed) 'Corrupt claim was not reported as a claimed blocker.'
		$list = Invoke-WorktreeCli @('plan', 'queue', 'list', '--repo', $fixture.Common, '--order', 'Documents/Plans/Order.md') 0
		$snapshot = ConvertFrom-AgentJson $list
		Assert-Equal @($snapshot.claims).Count 1 'Queue list did not skip exactly the corrupt claim.'
		Assert-True ($list.Stderr.IndexOf($claimPath, [StringComparison]::OrdinalIgnoreCase) -ge 0) 'Queue list diagnostic did not name the corrupt claim path.'
		$unclaim = Invoke-Process -FilePath $script:Executable -Arguments @('plan', 'row', 'unclaim', '--repo', $fixture.Common, '--order', 'Documents/Plans/Order.md', '--plan', 'A.md', '--owner', 'corrupt-owner')
		Assert-Equal $unclaim.ExitCode 1 'Row unclaim of the corrupt claim did not fail.'
		Assert-True ($unclaim.Stderr.IndexOf($claimPath, [StringComparison]::OrdinalIgnoreCase) -ge 0) 'Row unclaim failure did not name the corrupt claim path.'
		Remove-Item -LiteralPath $claimPath -Force
		$recovered = Invoke-ClaimNext $fixture $session 'recovered-owner' 'plans' 0 'Documents/Plans/A.md'
		Assert-Equal $recovered.Json.plan 'Documents/Plans/A.md' 'Deleting the corrupt claim file did not recover the row.'
		Invoke-RowUnclaim $fixture 'Documents/Plans/Order.md' 'A.md' 'recovered-owner'
		Invoke-RowUnclaim $fixture 'Documents/Plans/Order.md' 'B.md' 'skip-owner'
	}

	Invoke-Case 'complete tolerates an already-deleted plan file and leaves other plan files untouched' {
		$rows = @(
			New-Row 'Documents/Plans/A.md'
			New-Row 'Documents/Plans/B.md' 'Small' 2 3 1 @('Documents/Plans/A.md')
		)
		$fixture = New-FixtureRepository 'complete-absent' $rows @()
		$session = Add-Worktree $fixture 'session'
		Invoke-RowClaim $fixture 'Documents/Plans/Order.md' 'A.md' 'complete-owner' 'complete-session' $session
		# Phase 1 already git-rm'd the plan file in the session; complete runs against the store with the file absent.
		Invoke-Git $session @('rm', '--', 'Documents/Plans/A.md') | Out-Null
		$bPath = Join-Path $session 'Documents/Plans/B.md'
		$bBefore = [IO.File]::ReadAllBytes($bPath)
		$result = ConvertFrom-AgentJson (Invoke-WorktreeCli ((Get-OrderArguments $fixture 'complete' $session) + @('--owner','complete-owner','--session','complete-session','--plan','Documents/Plans/A.md')) 0)
		Assert-True $result.handled 'Complete with an absent plan file did not succeed.'
		Assert-Bytes $bPath $bBefore 'Complete deleted or modified an unrelated plan file.'
		$validation = Get-Validation $fixture $session
		Assert-True $validation.Json.ok 'Post-completion validate did not report ok.'
		Assert-Equal @($validation.Json.rows | Where-Object plan -CEQ 'Documents/Plans/A.md').Count 0 'Complete did not remove the row from the store.'
		Assert-Equal @($validation.Json.rows | Where-Object plan -CEQ 'Documents/Plans/B.md').Count 1 'Complete pruned an unrelated row.'
		Invoke-RowUnclaim $fixture 'Documents/Plans/Order.md' 'A.md' 'complete-owner'
	}

	Invoke-Case 'stale session cannot reclaim after a completion lands' {
		$fixture = New-FixtureRepository -Name 'stale' -PlansRows @((New-Row 'Documents/Plans/A.md'), (New-Row 'Documents/Plans/B.md')) -FeaturesRows @()
		$worker = Add-Worktree $fixture 'worker'
		$stale = Add-Worktree $fixture 'stale'
		$a = Invoke-ClaimNext $fixture $worker 'landing-owner' 'plans' 0 'Documents/Plans/A.md'
		Assert-True $a.Json.claimed 'Worker did not claim A.'
		$preLanding = Invoke-ClaimNext $fixture $stale 'prelanding-owner' 'plans' 2 'Documents/Plans/A.md'
		Assert-Equal $preLanding.Json.reason 'explicit-plan-blocked-or-missing' 'Pre-landing peer did not observe the live claim.'
		# Phase 1 deletes the plan file in the worker; the completion then lands by advancing primary.
		Invoke-Git $worker @('rm', '--', 'Documents/Plans/A.md') | Out-Null
		Invoke-Git $worker @('commit', '-m', 'complete A') | Out-Null
		$completedCommit = Invoke-Git $worker @('rev-parse', 'HEAD')
		Invoke-Git $fixture.Primary @('merge', '--ff-only', $completedCommit) | Out-Null
		# Phase 2 removes the row from the store against the advanced primary, then releases the claim.
		Invoke-WorktreeCli @('plan', 'order', 'complete', '--repo', $fixture.Common, '--worktree', $fixture.Primary, '--owner', 'landing-owner', '--session', 'landing-session', '--plan', 'Documents/Plans/A.md') 0 | Out-Null
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
	Invoke-Case 'torn complete heals a dangling cross-queue dependency edge on rerun' {
		# Cross-queue dependent F (features) depends on X (plans).
		$fixture = New-FixtureRepository 'torn-complete' @((New-Row 'Documents/Plans/X.md')) @((New-Row 'Documents/Features/F.txt' 'Small' 2 3 1 @('Documents/Plans/X.md')))
		Invoke-RowClaim $fixture 'Documents/Plans/Order.md' 'X.md' 'complete-owner' 'complete-session' $fixture.Primary
		# Simulate a torn phase-2 completion crash artifact: X's row is gone from the Plans store bytes while
		# X's owner claim survives. Direct store byte edits are allowed here to model the crash artifact.
		$plansStore = Get-StoreOrderPath $fixture 'Plans'
		$torn = ([IO.File]::ReadAllText($plansStore, $script:Utf8) -split "`n" | Where-Object { -not $_.StartsWith('| [X.md](X.md) |', [StringComparison]::Ordinal) }) -join "`n"
		[IO.File]::WriteAllText($plansStore, $torn, $script:Utf8)
		# The dangling edge (F depends on the now-absent X) is a blocking diagnostic until the rerun heals it.
		$tornValidation = Get-Validation $fixture $fixture.Primary 2
		Assert-True (@($tornValidation.Json.diagnostics.code) -ccontains 'missing-dependency') 'Torn store did not surface the dangling dependency.'
		# Rerun complete X: exempts the dangling-edge diagnostic, strips the edge, tolerates the absent row.
		$result = ConvertFrom-AgentJson (Invoke-WorktreeCli ((Get-OrderArguments $fixture 'complete' $fixture.Primary) + @('--owner','complete-owner','--session','complete-session','--plan','Documents/Plans/X.md')) 0)
		Assert-True $result.handled 'Torn complete rerun did not heal.'
		$healed = Get-Validation $fixture $fixture.Primary
		Assert-True $healed.Json.ok 'Healed queue did not validate ok:true (dangling edge survived).'
		Assert-Equal @($healed.Json.rows | Where-Object plan -CEQ 'Documents/Features/F.txt').Count 1 'Healed queue lost the dependent row.'
		Invoke-RowUnclaim $fixture 'Documents/Plans/Order.md' 'X.md' 'complete-owner'
	}

	Invoke-Case 'torn cross-queue add heals a half-written store on rerun' {
		$fixture = New-FixtureRepository 'torn-add' @() @()
		$session = Add-Worktree $fixture 'session'
		Set-Utf8File (Join-Path $session 'Documents/Features/Q.txt') "Q predecessor`n"
		Set-Utf8File (Join-Path $session 'Documents/Plans/P.md') "# P dependent`n"
		# Sequence: features predecessor Q then plans dependent P, so P gains Q as an implicit predecessor edge.
		$request = @{ schemaVersion=1; operation='add'; sequences = ,@(
			@{ queue='features'; plan='Documents/Features/Q.txt'; tier='Small'; effort=2; impact=3; risks=1; notes='predecessor'; dependsOn=@() },
			@{ queue='plans'; plan='Documents/Plans/P.md'; tier='Small'; effort=2; impact=3; risks=1; notes='dependent'; dependsOn=@() }
		) }
		$requestPath = Write-Request $session 'torn-add' $request
		$addArguments = (Get-OrderArguments $fixture 'add' $session) + @('--owner','add-owner','--session','add-session','--request',$requestPath)
		# Capture the empty Features store, run the full add (WorktreeCli renders P's dependency edge), then simulate
		# a torn add where only the Plans half was written: restore Features to its pre-add bytes so Q is absent.
		# Direct store byte edits are allowed here to model the crash artifact.
		$featuresEmpty = [IO.File]::ReadAllBytes((Get-StoreOrderPath $fixture 'Features'))
		Invoke-WorktreeCli $addArguments 0 | Out-Null
		[IO.File]::WriteAllBytes((Get-StoreOrderPath $fixture 'Features'), $featuresEmpty)
		$tornValidation = Get-Validation $fixture $session 2
		Assert-True (@($tornValidation.Json.diagnostics.code) -ccontains 'missing-dependency') 'Torn add store did not surface the dangling dependency.'
		# Rerun the SAME add request: exempts missing-dependency/duplicate-plan for the request plans and rewrites both halves.
		$result = ConvertFrom-AgentJson (Invoke-WorktreeCli $addArguments 0)
		Assert-True ($result.handled -and $result.operation -ceq 'add') 'Torn add rerun did not heal.'
		$healed = Get-Validation $fixture $session
		Assert-True $healed.Json.ok 'Healed add queue did not validate ok:true.'
		Assert-Equal @($healed.Json.rows | Where-Object plan -CEQ 'Documents/Plans/P.md').Count 1 'Healed add is missing the plans dependent.'
		Assert-Equal @($healed.Json.rows | Where-Object plan -CEQ 'Documents/Features/Q.txt').Count 1 'Healed add is missing the features predecessor.'
	}

	Invoke-Case 'init --force refuses while a plan-queue lock is held and leaves the store unchanged' {
		$fixture = New-FixtureRepository 'init-locked' @((New-Row 'Documents/Plans/A.md')) @()
		$plansBefore = [IO.File]::ReadAllBytes((Get-StoreOrderPath $fixture 'Plans'))
		$featuresBefore = [IO.File]::ReadAllBytes((Get-StoreOrderPath $fixture 'Features'))
		Invoke-QueueLock $fixture 'Documents/Plans/Order.md' 'lock-owner' 'lock-session' $fixture.Primary | Out-Null
		try {
			$refused = Invoke-Process -FilePath $script:Executable -Arguments @('plan', 'order', 'init', '--repo', $fixture.Common, '--worktree', $fixture.Primary, '--force')
			Assert-Equal $refused.ExitCode 2 'init --force did not refuse with a state conflict while the queue was locked.'
			Assert-True ($refused.Stderr.IndexOf('queue mutation in flight', [StringComparison]::OrdinalIgnoreCase) -ge 0) 'init refusal did not report the in-flight queue mutation.'
			Assert-Bytes (Get-StoreOrderPath $fixture 'Plans') $plansBefore 'Refused init mutated the Plans store.'
			Assert-Bytes (Get-StoreOrderPath $fixture 'Features') $featuresBefore 'Refused init mutated the Features store.'
		}
		finally { Invoke-QueueUnlock $fixture 'Documents/Plans/Order.md' 'lock-owner' }
		# With the lock released, init --force reseeds from the unchanged tree Order.md to identical bytes.
		Invoke-WorktreeCli @('plan', 'order', 'init', '--repo', $fixture.Common, '--worktree', $fixture.Primary, '--force') 0 | Out-Null
		Assert-Bytes (Get-StoreOrderPath $fixture 'Plans') $plansBefore 'Reseed after unlock changed the Plans store.'
	}

	Invoke-Case 'validate demotes a stale-baseline missing plan to a notice and add still lands beside it' {
		$fixture = New-FixtureRepository 'stale-baseline' @() @()
		# The session branches at the baseline, before the new plan lands on primary.
		$session = Add-Worktree $fixture 'session'
		# Land a new plan file on primary and publish its row against the primary tree (where the file exists).
		Set-Utf8File (Join-Path $fixture.Primary 'Documents/Plans/Landed.md') "# Landed`n"
		Invoke-Git $fixture.Primary @('add', '--', 'Documents/Plans/Landed.md') | Out-Null
		Invoke-Git $fixture.Primary @('commit', '-m', 'land a new plan on primary') | Out-Null
		$landRequest = @{ schemaVersion=1; operation='add'; sequences = ,@(
			@{ queue='plans'; plan='Documents/Plans/Landed.md'; tier='Small'; effort=2; impact=3; risks=1; notes='landed'; dependsOn=@() }
		) }
		$landPath = Write-Request $fixture.Primary 'stale-land' $landRequest
		Invoke-WorktreeCli ((Get-OrderArguments $fixture 'add' $fixture.Primary) + @('--owner','land-owner','--session','land-session','--request',$landPath)) 0 | Out-Null
		# (a) The stale session lacks Landed.md, but primary has it: validate demotes the missing-plan-file to a notice.
		$validation = Get-Validation $fixture $session 0
		Assert-True $validation.Json.ok 'Stale-baseline validate did not report ok:true.'
		Assert-Equal @($validation.Json.diagnostics).Count 0 'Stale-baseline missing plan produced a blocking diagnostic.'
		$notice = @($validation.Json.notices | Where-Object { $_.code -ceq 'missing-plan-file' -and $_.path -ceq 'Documents/Plans/Order.md' })
		Assert-Equal $notice.Count 1 'Stale-baseline missing plan was not demoted to a missing-plan-file notice.'
		# (c) A fresh valid add from the session succeeds despite the stale foreign row from (a).
		Set-Utf8File (Join-Path $session 'Documents/Plans/Fresh.md') "# Fresh`n"
		$freshRequest = @{ schemaVersion=1; operation='add'; sequences = ,@(
			@{ queue='plans'; plan='Documents/Plans/Fresh.md'; tier='Small'; effort=2; impact=3; risks=1; notes='fresh'; dependsOn=@() }
		) }
		$freshPath = Write-Request $session 'stale-fresh' $freshRequest
		$json = ConvertFrom-AgentJson (Invoke-WorktreeCli ((Get-OrderArguments $fixture 'add' $session) + @('--owner','fresh-owner','--session','fresh-session','--request',$freshPath)) 0)
		Assert-True ($json.handled -and $json.operation -ceq 'add') 'Fresh add beside a stale foreign row did not succeed.'
		$afterFresh = Get-Validation $fixture $session 0
		Assert-Equal @($afterFresh.Json.rows | Where-Object plan -CEQ 'Documents/Plans/Fresh.md').Count 1 'Fresh add did not publish its row.'
	}

	Invoke-Case 'validate keeps a missing plan blocking when it is absent from both trees' {
		$fixture = New-FixtureRepository 'absent-both' @() @()
		$session = Add-Worktree $fixture 'session'
		# Land Ghost.md on primary and publish its row, then remove the file from primary while keeping the row.
		Set-Utf8File (Join-Path $fixture.Primary 'Documents/Plans/Ghost.md') "# Ghost`n"
		Invoke-Git $fixture.Primary @('add', '--', 'Documents/Plans/Ghost.md') | Out-Null
		Invoke-Git $fixture.Primary @('commit', '-m', 'land ghost plan') | Out-Null
		$request = @{ schemaVersion=1; operation='add'; sequences = ,@(
			@{ queue='plans'; plan='Documents/Plans/Ghost.md'; tier='Small'; effort=2; impact=3; risks=1; notes='ghost'; dependsOn=@() }
		) }
		$requestPath = Write-Request $fixture.Primary 'absent-add' $request
		Invoke-WorktreeCli ((Get-OrderArguments $fixture 'add' $fixture.Primary) + @('--owner','ghost-owner','--session','ghost-session','--request',$requestPath)) 0 | Out-Null
		Invoke-Git $fixture.Primary @('rm', '--', 'Documents/Plans/Ghost.md') | Out-Null
		Invoke-Git $fixture.Primary @('commit', '-m', 'remove ghost file, keep its row') | Out-Null
		# Absent from the session baseline and from primary: the missing-plan-file stays a blocking diagnostic.
		$validation = Get-Validation $fixture $session 2
		Assert-True (-not $validation.Json.ok) 'Both-absent missing plan did not report ok:false.'
		$blocking = @($validation.Json.diagnostics | Where-Object { $_.code -ceq 'missing-plan-file' -and $_.path -ceq 'Documents/Plans/Order.md' })
		Assert-Equal $blocking.Count 1 'Both-absent missing plan was not a blocking missing-plan-file diagnostic.'
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
	if ($null -eq $script:OriginalLocalAppData) { [Environment]::SetEnvironmentVariable('LOCALAPPDATA', $null) }
	else { $env:LOCALAPPDATA = $script:OriginalLocalAppData }
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

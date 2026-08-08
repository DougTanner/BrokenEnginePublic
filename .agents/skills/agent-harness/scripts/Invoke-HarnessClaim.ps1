# Require the project executables, provision the checkout, resolve AgentHarness, mint an owner
# token, and claim the harness lock.
# The script never steals, never waits for a lock, and never touches a foreign owner's processes:
# a blocked claim returns immediately with the holder record the claim itself printed.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $RepositoryRoot,
	[Parameter(Mandatory)][string] $Session,
	[string] $Key = 'default',
	[string] $Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$MaximumMessageLength = 256
$MaximumOutputCharacters = 2048

$result = [ordered]@{
	schemaVersion = 'broken-engine-harness-claim/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Harness claim did not run.'
	repositoryRoot = $RepositoryRoot
	session = $Session
	key = $Key
	owner = $null
	agentHarness = $null
	claim = $null
	currentOwner = $null
	provision = $null
}

function Limit-Text([string] $Value, [int] $Maximum) {
	$normalized = ([string]$Value).Trim()
	if ($normalized.Length -le $Maximum) { return $normalized }
	return $normalized.Substring(0, $Maximum - 3) + '...'
}

function Limit-TrailingText([string] $Value, [int] $Maximum) {
	$normalized = ([string]$Value).Trim()
	if ($normalized.Length -le $Maximum) { return $normalized }
	return '...' + $normalized.Substring($normalized.Length - ($Maximum - 3))
}

function Complete-HarnessClaim([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = Limit-Text $Message $MaximumMessageLength
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 32 -Compress))
	exit $ExitCode
}

function Invoke-NativeCapture([string] $FilePath, [string[]] $Arguments) {
	$raw = @(& $FilePath @Arguments 2>&1)
	$exitCode = $LASTEXITCODE
	$standard = @($raw | Where-Object { $_ -isnot [System.Management.Automation.ErrorRecord] })
	$failed = @($raw | Where-Object { $_ -is [System.Management.Automation.ErrorRecord] })
	return [pscustomobject]@{
		ExitCode = $exitCode
		Stdout = [string]::Join([Environment]::NewLine, @($standard | ForEach-Object { [string]$_ }))
		Stderr = [string]::Join([Environment]::NewLine, @($failed | ForEach-Object { [string]$_ }))
	}
}

function ConvertFrom-LockOutput([string] $Text) {
	if ([string]::IsNullOrWhiteSpace($Text)) { return $null }
	try { return $Text | ConvertFrom-Json -Depth 32 } catch { return $null }
}

function Get-LockField($Record, [string] $Name) {
	if ($null -eq $Record -or -not ($Record.PSObject.Properties.Name -ccontains $Name)) { return $null }
	return $Record.$Name
}

try {
	if ([string]::IsNullOrWhiteSpace($Session)) { throw 'Session must not be empty.' }
	if ([string]::IsNullOrWhiteSpace($Key)) { throw 'Key must not be empty.' }
	if ([string]::IsNullOrWhiteSpace($Configuration)) { throw 'Configuration must not be empty.' }
	if (-not [IO.Path]::IsPathRooted($RepositoryRoot)) { throw "RepositoryRoot must be absolute: '$RepositoryRoot'." }

	$sharedScripts = Join-Path $PSScriptRoot '..\..\..\scripts'
	if (-not (Test-Path -LiteralPath (Join-Path $sharedScripts 'AgentScriptCommon.psm1'))) {
		$sharedScripts = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts'
	}
	Import-Module (Join-Path $sharedScripts 'AgentScriptCommon.psm1') -Force
	$root = Get-AgentCanonicalPath $RepositoryRoot
	$result.repositoryRoot = $root
	if (-not (Test-Path -LiteralPath $root -PathType Container)) {
		Complete-HarnessClaim 1 'error' 'claim.repository-missing' "RepositoryRoot is not a directory: '$root'."
	}

	$provisioner = [IO.Path]::GetFullPath((Join-Path $sharedScripts 'Provision-WorktreeThirdParty.ps1'))
	if (-not (Test-Path -LiteralPath $provisioner -PathType Leaf)) {
		Complete-HarnessClaim 1 'error' 'claim.provisioner-missing' "Provisioning script is missing: '$provisioner'."
	}

	# The project harness doc owns the executable names and their Output directory, so the launch
	# block stays the single source of truth: only those three assignments are read, and the doc's
	# hardcoded Debug suffix becomes the requested configuration. Missing executables block here,
	# before provisioning and before the lock, so a build routes through /compile and re-enters.
	$harnessDocument = Join-Path $root 'Projects\BrokenEngineSandbox\Documents\AgentHarness.md'
	$launchText = ''
	if (Test-Path -LiteralPath $harnessDocument -PathType Leaf) {
		# A read failure leaves the text empty so it reports the same unreadable-doc code as a parse failure.
		$launchText = [string](Get-Content -LiteralPath $harnessDocument -Raw -ErrorAction SilentlyContinue)
	}
	$outputMatch = [regex]::Match($launchText, '(?m)^\s*\$Output\s*=\s*Join-Path \$ROOT ''([^'']+)''\s*$')
	$serverMatch = [regex]::Match($launchText, '(?m)^\s*\$ServerExe\s*=\s*Join-Path \$Output ''([^'']+)''\s*$')
	$clientMatch = [regex]::Match($launchText, '(?m)^\s*\$ClientExe\s*=\s*Join-Path \$Output ''([^'']+)''\s*$')
	if (-not $outputMatch.Success -or -not $serverMatch.Success -or -not $clientMatch.Success) {
		Complete-HarnessClaim 1 'error' 'claim.launch-doc-unreadable' `
			"Output, ServerExe, and ClientExe could not be read from '$harnessDocument'."
	}
	$outputRelative = $outputMatch.Groups[1].Value
	$outputDirectory = Join-Path $root $outputRelative
	$missingExecutables = @()
	foreach ($executable in @($serverMatch.Groups[1].Value, $clientMatch.Groups[1].Value)) {
		$configured = [regex]::Replace($executable, '\.Debug\.exe$', ".$Configuration.exe")
		if (-not (Test-Path -LiteralPath (Join-Path $outputDirectory $configured) -PathType Leaf)) {
			$missingExecutables += $configured
		}
	}
	if ($missingExecutables.Count -gt 0) {
		Complete-HarnessClaim 2 'blocked' 'claim.executable-missing' `
			"Missing executables in '$outputRelative': $($missingExecutables -join ', ')."
	}

	# The provisioner throws instead of exiting, so it runs as a child process whose exit code
	# reports the failure and whose progress output stays out of this script's stdout.
	$powerShell = [Environment]::ProcessPath
	if ([string]::IsNullOrWhiteSpace($powerShell)) { $powerShell = 'pwsh' }
	$provision = Invoke-NativeCapture $powerShell @(
		'-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $provisioner, '-RepositoryRoot', $root)
	$provisionOutput = Limit-TrailingText (($provision.Stdout, $provision.Stderr) -join [Environment]::NewLine) $MaximumOutputCharacters
	$result.provision = [ordered]@{ exitCode = $provision.ExitCode; output = $provisionOutput }
	if ($provision.ExitCode -ne 0) {
		Complete-HarnessClaim 1 'error' 'claim.provision-failed' "Worktree provisioning failed with exit $($provision.ExitCode): $provisionOutput"
	}

	$agentHarness = Join-Path $root 'Tools\AgentHarness\Platforms\VisualStudio2026\Output\AgentHarness.exe'
	if (-not (Test-Path -LiteralPath $agentHarness -PathType Leaf)) {
		Complete-HarnessClaim 1 'error' 'claim.harness-missing' "AgentHarness executable is missing: '$agentHarness'."
	}
	$result.agentHarness = $agentHarness

	$token = Invoke-NativeCapture $agentHarness @('lock', 'token')
	$owner = $token.Stdout.Trim()
	if ($token.ExitCode -ne 0 -or $owner -cnotmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') {
		Complete-HarnessClaim 1 'error' 'claim.token-failed' "AgentHarness could not mint an owner token (exit $($token.ExitCode)): $($token.Stderr)"
	}
	$result.owner = $owner

	$claim = Invoke-NativeCapture $agentHarness @(
		'lock', 'claim', '--key', $Key, '--owner', $owner, '--session', $Session, '--worktree', $root)
	$record = ConvertFrom-LockOutput $claim.Stdout
	if ($claim.ExitCode -eq 0) {
		if ($null -eq $record) {
			Complete-HarnessClaim 1 'error' 'claim.metadata-unreadable' 'AgentHarness claimed the lock but did not print a readable lock record.'
		}
		$result.claim = $record
		Complete-HarnessClaim 0 'pass' 'ok' "Harness lock '$Key' claimed by owner $owner."
	}
	if ($claim.ExitCode -eq 2) {
		# lock claim prints the held record itself, so the holder needs no second lock status call.
		$claimedAt = Get-LockField $record 'claimedAt'
		# ConvertFrom-Json turns an ISO-8601 timestamp into [datetime], so both forms are normalized here.
		$claimedUtc = $null
		if ($claimedAt -is [datetime]) {
			$claimedUtc = ([datetime]$claimedAt).ToUniversalTime()
		}
		elseif ($claimedAt -is [string]) {
			try {
				$claimedUtc = [datetime]::Parse($claimedAt, [cultureinfo]::InvariantCulture,
					[Globalization.DateTimeStyles]::AdjustToUniversal -bor [Globalization.DateTimeStyles]::AssumeUniversal)
			}
			catch { $claimedUtc = $null }
		}
		$holdSeconds = $null
		$claimedAtText = [string]$claimedAt
		if ($null -ne $claimedUtc) {
			$holdSeconds = [Math]::Round(([datetime]::UtcNow - $claimedUtc).TotalSeconds, 3)
			$claimedAtText = $claimedUtc.ToString('yyyy-MM-ddTHH:mm:ss.fffZ', [cultureinfo]::InvariantCulture)
		}
		$result.currentOwner = [ordered]@{
			owner = Get-LockField $record 'owner'
			session = Get-LockField $record 'session'
			worktree = Get-LockField $record 'worktree'
			claimedAt = $claimedAtText
			heartbeatAt = Get-LockField $record 'heartbeatAt'
			holdSeconds = $holdSeconds
			record = $record
		}
		Complete-HarnessClaim 2 'blocked' 'claim.foreign-owner' "Harness lock '$Key' is held by owner $(Get-LockField $record 'owner') since $claimedAtText ($holdSeconds seconds)."
	}
	Complete-HarnessClaim 1 'error' 'claim.failed' "AgentHarness lock claim failed with exit $($claim.ExitCode): $($claim.Stderr)"
}
catch {
	Complete-HarnessClaim 1 'error' 'internal.error' $_.Exception.Message
}

# Sole SmartGit review-tool boundary for a session landing.
#
# Invoked last in workflow step 4 — after approval preparation returned the
# approved tip — as the final tool invocation before the landing summary renders.
# Opening the log window here means the user returns from their review to a
# finished confirmation question rather than a working agent. Callers never
# reconstruct its SmartGit command inline.
#
# The landing route always passes -LaunchSmartGit, so a session landing always
# attempts the launch and the window opens whenever SmartGit is available.
# Without the switch it only previews the canonical command.
# It never runs Git, mutates a ref, or claims a lock.
#
# Contract: schema broken-engine-finalize-approval-review/v1. Unlike the mutating
# scripts, every preview/launch outcome exits 0 and none report status pass — the
# review tool is non-blocking and never gates a landing. preview is the default;
# opened is the launch success path; unavailable/failed are non-blocking,
# and the caller copies message and the exact manualCommand into the approval response while keeping the landing gate in
# force. Only invalid input exits 1, with status error and a code naming the cause
# (input.invalid for a malformed tip or fixture gate; internal.error for a primary
# worktree that will not resolve). A clean identical post-confirmation rebase that
# preserves the existing confirmation must not reopen the review tool the user
# already saw: that path does not invoke this script at all. A material change
# requiring a refreshed confirmation does invoke it again, against the newly
# reviewed candidate, before that refreshed confirmation.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $PrimaryWorktree,
	[Parameter(Mandatory)][string] $ApprovedTip,
	[switch] $LaunchSmartGit,
	[AllowEmptyString()][string] $FixtureSmartGitExecutable,
	[ValidateSet('none', 'smartgit-launch')][string] $FixtureFailure = 'none'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$workflowModule = Join-Path $PSScriptRoot '..\..\..\scripts\FinalizeWorkflowCommon.psm1'
if (-not (Test-Path -LiteralPath $workflowModule)) {
	$workflowModule = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts\FinalizeWorkflowCommon.psm1'
}
Import-Module $workflowModule -Force

$result = [ordered]@{
	schemaVersion = 'broken-engine-finalize-approval-review/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Review-tool launch did not complete.'
	executable = $null
	arguments = @()
	manualCommand = $null
	processId = $null
}

$script:PrimaryIdentity = $null
$script:HasFixtureSmartGitExecutable = $PSBoundParameters.ContainsKey('FixtureSmartGitExecutable')

function Complete-Review([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message)
{
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	Write-Output ($result | ConvertTo-Json -Depth 12 -Compress)
	exit $ExitCode
}

function Throw-Review([int] $ExitCode, [string] $Code, [string] $Message)
{
	$exception = [InvalidOperationException]::new($Message)
	$exception.Data['ExitCode'] = $ExitCode
	$exception.Data['Code'] = $Code
	throw $exception
}

function Assert-Input([bool] $Condition, [string] $Message)
{
	if (-not $Condition)
	{
		Throw-Review 1 'input.invalid' $Message
	}
}

function ConvertTo-PowerShellLiteral([string] $Value)
{
	return "'" + $Value.Replace("'", "''") + "'"
}

function ConvertTo-ProcessArgument([string] $Value)
{
	if ($Value.Contains('"', [StringComparison]::Ordinal))
	{
		throw 'SmartGit arguments cannot contain a double quote.'
	}
	return '"' + $Value + '"'
}

function Invoke-SmartGit
{
	$standardExecutable = 'C:\Program Files\SmartGit\bin\smartgit.exe'
	$arguments = @('--log', $script:PrimaryIdentity, "--anchor-commit=$ApprovedTip")
	$result.arguments = $arguments
	$result.manualCommand = ((@('&', (ConvertTo-PowerShellLiteral $standardExecutable)) + @($arguments | ForEach-Object { ConvertTo-PowerShellLiteral $_ })) -join ' ')
	if (-not $LaunchSmartGit)
	{
		Complete-Review 0 'preview' 'review.preview' 'SmartGit was not launched; run manualCommand to open the candidate.'
	}
	$resolvedExecutable = $null
	if ($script:HasFixtureSmartGitExecutable)
	{
		$resolvedExecutable = $FixtureSmartGitExecutable
	}
	elseif (Test-Path -LiteralPath $standardExecutable -PathType Leaf)
	{
		$resolvedExecutable = $standardExecutable
	}
	else
	{
		$command = Get-Command smartgit.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
		if ($null -ne $command)
		{
			$resolvedExecutable = $command.Source
		}
	}
	if ([string]::IsNullOrWhiteSpace($resolvedExecutable) -or -not (Test-Path -LiteralPath $resolvedExecutable -PathType Leaf))
	{
		Complete-Review 0 'unavailable' 'review.unavailable' 'SmartGit executable was not found.'
	}

	$result.executable = [IO.Path]::GetFullPath($resolvedExecutable)
	try
	{
		if ($FixtureFailure -ceq 'smartgit-launch')
		{
			throw 'Fixture forced a SmartGit launch failure.'
		}
		$processArguments = @($arguments | ForEach-Object { ConvertTo-ProcessArgument $_ })
		$process = Start-Process -FilePath $result.executable -ArgumentList $processArguments -PassThru
		$result.processId = $process.Id
		$process.Dispose()
	}
	catch
	{
		Complete-Review 0 'failed' 'review.launch-failed' $_.Exception.Message
	}
	Complete-Review 0 'opened' 'ok' 'SmartGit was opened for the approval candidate.'
}

try
{
	Assert-Input ($ApprovedTip -cmatch '^[0-9a-f]{40}$') 'ApprovedTip must be exactly 40 lowercase hexadecimal characters.'
	if ($FixtureFailure -cne 'none' -or $script:HasFixtureSmartGitExecutable)
	{
		Assert-Input ($env:BROKEN_ENGINE_FINALIZE_APPROVAL_PREPARATION_FIXTURE -ceq '1') 'Fixture-only inputs require the finalization preparation fixture environment.'
	}
	$script:PrimaryIdentity = Get-FinalizeExistingWindowsIdentity $PrimaryWorktree 'Primary worktree'

	Invoke-SmartGit
}
catch
{
	$failure = $_.Exception
	$exitCode = if ($failure.Data.Contains('ExitCode')) { [int]$failure.Data['ExitCode'] } else { 1 }
	$code = if ($failure.Data.Contains('Code')) { [string]$failure.Data['Code'] } else { 'internal.error' }
	Complete-Review $exitCode 'error' $code $failure.Message
}

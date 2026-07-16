[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $ReportPath,
	[Parameter(Mandatory)][string] $Worktree,
	[Parameter(Mandatory)][string] $Baseline,
	[string] $ManifestComparisonBase,
	[Parameter(Mandatory)][string] $PlanIntent,
	[Parameter(Mandatory)][string[]] $AcceptanceLedgerLine,
	[Parameter(Mandatory)][string[]] $QueueReceiptOrResidualLine
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$result = [ordered]@{
	schemaVersion = 'broken-engine-verification-report/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Verification report was not written.'
	reportPath = $null
	worktree = $null
	baseline = $Baseline
	comparisonBase = $null
	sha256 = $null
	manifest = [ordered]@{ range = $null; sha256 = $null; count = 0 }
}

function Complete-VerificationReport([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 8 -Compress))
	exit $ExitCode
}

function Assert-SingleLine([string[]] $Lines, [string] $Label) {
	if ($Lines.Count -eq 0) { throw "$Label must contain at least one line." }
	foreach ($line in $Lines) {
		if ([string]::IsNullOrEmpty($line) -or $line.IndexOf("`r", [StringComparison]::Ordinal) -ge 0 -or $line.IndexOf("`n", [StringComparison]::Ordinal) -ge 0) {
			throw "$Label must contain nonempty single-line values."
		}
	}
}

function Test-PathContainedBy([string] $Root, [string] $Candidate) {
	$relative = [IO.Path]::GetRelativePath($Root, $Candidate)
	return -not [IO.Path]::IsPathRooted($relative) -and $relative -ne '..' -and
		-not $relative.StartsWith("..$([IO.Path]::DirectorySeparatorChar)", [StringComparison]::Ordinal) -and
		-not $relative.StartsWith("..$([IO.Path]::AltDirectorySeparatorChar)", [StringComparison]::Ordinal)
}

try {
	if ($Baseline -cnotmatch '^[0-9a-f]{40}$') { throw 'Baseline must be exactly 40 lowercase hexadecimal characters.' }
	$comparisonBase = if ([string]::IsNullOrWhiteSpace($ManifestComparisonBase)) { $Baseline } else { $ManifestComparisonBase }
	if ($comparisonBase -cnotmatch '^[0-9a-f]{40}$') { throw 'ManifestComparisonBase must be exactly 40 lowercase hexadecimal characters.' }
	if ([string]::IsNullOrWhiteSpace($PlanIntent) -or $PlanIntent.IndexOf("`r", [StringComparison]::Ordinal) -ge 0 -or $PlanIntent.IndexOf("`n", [StringComparison]::Ordinal) -ge 0) {
		throw 'PlanIntent must be one nonempty line.'
	}
	Assert-SingleLine $AcceptanceLedgerLine 'AcceptanceLedgerLine'
	Assert-SingleLine $QueueReceiptOrResidualLine 'QueueReceiptOrResidualLine'

	$module = Join-Path $PSScriptRoot 'FinalizeWorkflowCommon.psm1'
	$artifactStore = Join-Path $PSScriptRoot 'AgentArtifactStore.psm1'
	Import-Module $module -Force
	Import-Module $artifactStore -Force -DisableNameChecking
	$identity = Get-FinalizeGitIdentity $Worktree 'Verification worktree'
	if (-not (Test-FinalizeGitSuccess $identity.Worktree @('rev-parse', '--verify', "${Baseline}^{commit}"))) {
		throw "Baseline is not a commit in this repository: '$Baseline'."
	}
	if (-not (Test-FinalizeGitSuccess $identity.Worktree @('rev-parse', '--verify', "${comparisonBase}^{commit}"))) {
		throw "ManifestComparisonBase is not a commit in this repository: '$comparisonBase'."
	}
	$report = Assert-AgentArtifactPath -Worktree $identity.Worktree -Path $ReportPath
	if (Test-Path -LiteralPath $report) { throw "ReportPath already exists and reports are immutable: '$report'." }

	$manifestRows = @(Get-FinalizeManifestRows $identity.Worktree $comparisonBase)
	if ($manifestRows.Count -eq 0) { throw 'Final manifest is empty; no evidence-bearing report can be written.' }
	$manifestHash = Get-FinalizeManifestSha256 $manifestRows
	$lines = [Collections.Generic.List[string]]::new()
	foreach ($line in @(
		'Schema: be-agent-report/v1',
		"Worktree: $($identity.Worktree)",
		"Baseline: $Baseline",
		"Plan/intent: $PlanIntent",
		'',
		'## Acceptance ledger'
	)) { $lines.Add($line) }
	foreach ($line in $AcceptanceLedgerLine) { $lines.Add($line) }
	$lines.Add('')
	$lines.Add('## Final manifest')
	$manifestStart = $lines.Count + 1
	foreach ($row in $manifestRows) { $lines.Add($row) }
	$manifestEnd = $lines.Count
	$lines.Add('')
	$lines.Add('## Queue receipts and residuals')
	foreach ($line in $QueueReceiptOrResidualLine) { $lines.Add($line) }

	$utf8 = [Text.UTF8Encoding]::new($false, $true)
	$temp = Join-Path ([IO.Path]::GetDirectoryName($report)) ('.' + [IO.Path]::GetFileName($report) + '.' + [guid]::NewGuid().ToString('N') + '.tmp')
	try {
		[IO.File]::WriteAllText($temp, ($lines -join "`n") + "`n", $utf8)
		[IO.File]::Move($temp, $report)
	}
	finally {
		if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Force }
	}
	$bytes = [IO.File]::ReadAllBytes($report)
	$result.reportPath = $report
	$result.worktree = $identity.Worktree
	$result.comparisonBase = $comparisonBase
	$result.sha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
	$result.manifest.range = "L$manifestStart-L$manifestEnd"
	$result.manifest.sha256 = $manifestHash
	$result.manifest.count = $manifestRows.Count
	Complete-VerificationReport 0 'pass' 'ok' 'Verification report and raw manifest were written.'
}
catch {
	Complete-VerificationReport 1 'error' 'report.write-failed' $_.Exception.Message
}

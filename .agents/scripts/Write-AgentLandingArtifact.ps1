[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $Worktree,
	[Parameter(Mandatory)][string] $Commit,
	[Parameter(Mandatory)][string] $VerificationReportPath,
	[Parameter(Mandatory)][string] $VerificationReportSha256,
	[Parameter(Mandatory)][string[]] $ManifestRange,
	[Parameter(Mandatory)][string] $SessionOwner,
	[Parameter(Mandatory)][string] $SessionLabel,
	[string] $Client = 'unknown',
	[string] $CompletedPlanReceipt
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$commonModule = Join-Path $PSScriptRoot 'FinalizeWorkflowCommon.psm1'
$storeModule = Join-Path $PSScriptRoot 'AgentArtifactStore.psm1'
Import-Module $commonModule -Force
Import-Module $storeModule -Force -DisableNameChecking

if ($VerificationReportSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'VerificationReportSha256 must be exactly 64 lowercase hexadecimal characters.' }
if ($SessionOwner -notmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') { throw 'SessionOwner must be a canonical GUID.' }
if ([string]::IsNullOrWhiteSpace($SessionLabel) -or $SessionLabel.IndexOfAny([char[]]@("`r", "`n")) -ge 0) { throw 'SessionLabel must be one nonempty line.' }
if ($Client -notin @('claude', 'codex', 'unknown')) { throw "Client is invalid: '$Client'." }

$identity = Get-FinalizeGitIdentity $Worktree 'Landing artifact worktree'
$commitHash = (Invoke-FinalizeGit $identity.Worktree @('rev-parse', "$Commit^{commit}")).Trim()
$report = Assert-AgentArtifactPath -Worktree $identity.Worktree -Path $VerificationReportPath
if (-not (Test-Path -LiteralPath $report -PathType Leaf)) { throw "Verification report does not exist: '$report'." }
$actualReportHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([IO.File]::ReadAllBytes($report))).ToLowerInvariant()
if ($actualReportHash -cne $VerificationReportSha256) { throw "Verification report SHA-256 mismatch: '$report'." }
$normalizedManifestRanges = @($ManifestRange | ForEach-Object { $_ -split ',' } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
if ($normalizedManifestRanges.Count -eq 0) { throw 'At least one manifest range is required.' }

$transcript = [ordered]@{ status = 'not-collected'; candidates = @() }
if ($Client -eq 'codex') {
	$finder = Join-Path $PSScriptRoot '..\skills\next-plan-review\scripts\Find-AgentSessionTranscript.ps1'
	$finderResult = & $finder -Commit $commitHash -RepositoryRoot $identity.Worktree
	$finderJson = $finderResult | ConvertFrom-Json -Depth 32 -ErrorAction Stop
	if ($finderJson.schema -cne 'broken-engine-codex-transcript-candidates/v1') { throw "Transcript finder returned an unexpected schema: '$($finderJson.schema)'." }
	$transcript.status = if (@($finderJson.candidates).Count -gt 0) { 'candidate-found' } else { 'blocked' }
	$transcript.candidates = @($finderJson.candidates)
}

$artifactPath = Get-AgentLandingArtifactPath -Worktree $identity.Worktree -Commit $commitHash
if (Test-Path -LiteralPath $artifactPath -PathType Leaf) {
	$existing = & (Join-Path $PSScriptRoot 'Find-AgentLandingArtifact.ps1') -RepositoryRoot $identity.Worktree -Commit $commitHash
	$existingJson = $existing | ConvertFrom-Json -Depth 32 -ErrorAction Stop
	if ($existingJson.schema -cne 'broken-engine-agent-landing-artifact-lookup/v1' -or $existingJson.status -cne 'found' -or $existingJson.commit -cne $commitHash) {
		throw "Existing landing artifact validation did not bind commit '$commitHash': '$artifactPath'."
	}
	[pscustomobject]@{ schema = 'broken-engine-agent-landing-artifact-write/v1'; status = 'exists'; artifactPath = $artifactPath; commit = $commitHash } | ConvertTo-Json -Depth 8
	exit 0
}

$receipt = $null
if (-not [string]::IsNullOrWhiteSpace($CompletedPlanReceipt)) {
	try { $receipt = $CompletedPlanReceipt | ConvertFrom-Json -Depth 32 -ErrorAction Stop }
	catch { throw 'CompletedPlanReceipt must be valid JSON when supplied.' }
}
$artifact = [ordered]@{
	schema = 'broken-engine-agent-landing-artifact/v1'
	commit = $commitHash
	createdUtc = [DateTimeOffset]::UtcNow.ToString('O')
	repository = [ordered]@{ worktree = $identity.Worktree; commonDirectory = $identity.CommonDirectory; branch = $identity.Branch }
	session = [ordered]@{ client = $Client; owner = $SessionOwner; label = $SessionLabel }
	verification = [ordered]@{ reportPath = $report; sha256 = $VerificationReportSha256; manifestRanges = @($normalizedManifestRanges) }
	completedPlanReceipt = $receipt
	transcript = $transcript
}
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$temp = Join-Path ([IO.Path]::GetDirectoryName($artifactPath)) ('.' + [IO.Path]::GetFileName($artifactPath) + '.' + [guid]::NewGuid().ToString('N') + '.tmp')
try {
	[IO.File]::WriteAllText($temp, ($artifact | ConvertTo-Json -Depth 32) + "`n", $utf8)
	[IO.File]::Move($temp, $artifactPath)
}
finally {
	if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Force }
}
$artifactHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([IO.File]::ReadAllBytes($artifactPath))).ToLowerInvariant()
[pscustomobject]@{ schema = 'broken-engine-agent-landing-artifact-write/v1'; status = 'written'; artifactPath = $artifactPath; sha256 = $artifactHash; commit = $commitHash; transcriptStatus = $transcript.status } | ConvertTo-Json -Depth 8

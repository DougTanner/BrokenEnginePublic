Set-StrictMode -Version Latest

$script:NextPlanUtf8 = [Text.UTF8Encoding]::new($false, $true)
$sharedScripts = Join-Path $PSScriptRoot '..\..\..\scripts'
Import-Module (Join-Path $sharedScripts 'FinalizeWorkflowCommon.psm1') -Force
Import-Module (Join-Path $sharedScripts 'AgentArtifactStore.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $sharedScripts 'WorktreeCliSessionExclusion.psm1') -Force -DisableNameChecking

function New-NextPlanStateBlocker([string] $Message) {
	$exception = [InvalidOperationException]::new($Message)
	$exception.Data['NextPlanExitCode'] = 2
	return $exception
}

function Test-NextPlanStateBlocker($ErrorRecord) {
	return $null -ne $ErrorRecord -and $null -ne $ErrorRecord.Exception -and
		$ErrorRecord.Exception.Data.Contains('NextPlanExitCode') -and
		[int]$ErrorRecord.Exception.Data['NextPlanExitCode'] -eq 2
}

function Get-NextPlanEnvironmentValue([string] $Name) {
	$value = [Environment]::GetEnvironmentVariable($Name)
	if ([string]::IsNullOrWhiteSpace($value) -or $value.IndexOf("`r", [StringComparison]::Ordinal) -ge 0 -or $value.IndexOf("`n", [StringComparison]::Ordinal) -ge 0) {
		throw "Wrapper environment variable $Name is missing or invalid."
	}
	return $value
}

function Get-NextPlanContext([switch] $RequireCleanSession, [switch] $RequireCleanPrimary, [switch] $AllowPrimaryAdvance) {
	try {
	if ((Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_WORKTREECLI_ADMISSION_MODE') -cne 'session') {
		throw 'The wrapper admission mode is not session.'
	}
	$worktreeValue = Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_WORKTREE_PATH'
	$sessionWorktreeValue = Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_WORKTREECLI_SESSION_WORKTREE'
	$primaryValue = Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_PRIMARY_CHECKOUT'
	$sessionBranch = Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_SESSION_BRANCH'
	$targetBranch = Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_TARGET_BRANCH'
	$baseline = Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_BASELINE'
	$owner = Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER'
	if ($baseline -cnotmatch '^[0-9a-f]{40}$') { throw 'BROKEN_ENGINE_BASELINE is not a lowercase commit ID.' }

	$worktree = Get-FinalizeGitIdentity $worktreeValue 'Wrapper session worktree'
	$sessionWorktree = Get-FinalizeExistingWindowsIdentity $sessionWorktreeValue 'Registered wrapper session worktree'
	$current = Get-FinalizeExistingWindowsIdentity (Get-Location).Path 'Current directory'
	if (-not $sessionWorktree.Equals($worktree.Worktree, [StringComparison]::OrdinalIgnoreCase) -or
		-not $current.Equals($worktree.Worktree, [StringComparison]::OrdinalIgnoreCase)) {
		throw (New-NextPlanStateBlocker 'Current, registered, and wrapper session worktree identities do not match.')
	}
	$primary = Get-FinalizeGitIdentity $primaryValue 'Wrapper primary checkout'
	if (-not $primary.CommonDirectory.Equals($worktree.CommonDirectory, [StringComparison]::OrdinalIgnoreCase)) {
		throw (New-NextPlanStateBlocker 'Wrapper primary and session worktrees do not share a Git common directory.')
	}
	$primaryDotGit = Get-Item -LiteralPath (Join-Path $primary.Worktree '.git') -Force -ErrorAction Stop
	if (-not $primaryDotGit.PSIsContainer -or ($primaryDotGit.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
		throw (New-NextPlanStateBlocker 'BROKEN_ENGINE_PRIMARY_CHECKOUT is not the primary checkout.')
	}
	if ($worktree.Branch -cne $sessionBranch -or $primary.Branch -cne $targetBranch) {
		throw (New-NextPlanStateBlocker 'Wrapper branch identities do not match the attached Git branches.')
	}
	if ($worktree.Head -cne $baseline -or (-not $AllowPrimaryAdvance -and $primary.Head -cne $baseline)) {
		throw (New-NextPlanStateBlocker 'Session or primary HEAD moved from the wrapper baseline.')
	}
	if ($RequireCleanSession -and -not [string]::IsNullOrEmpty((Invoke-FinalizeGit $worktree.Worktree @('status', '--porcelain=v1', '--untracked-files=normal')))) {
		throw (New-NextPlanStateBlocker 'Session worktree is not clean.')
	}
	if ($RequireCleanPrimary -and -not [string]::IsNullOrEmpty((Invoke-FinalizeGit $primary.Worktree @('status', '--porcelain=v1', '--untracked-files=normal')))) {
		throw (New-NextPlanStateBlocker 'Primary checkout is not clean.')
	}

	$classification = Get-WorktreeCliSessionClassification -RepositoryRoot $worktree.Worktree -Owner $owner -Worktree $worktree.Worktree
	if ($classification.Classification -cne 'expected-live') {
		throw (New-NextPlanStateBlocker "Wrapper WorktreeCli session classification is '$($classification.Classification)', expected 'expected-live'.")
	}

	$relativeOutput = 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
	$sessionOutput = Join-Path $worktree.Worktree $relativeOutput
	$primaryOutput = Join-Path $primary.Worktree $relativeOutput
	if (-not (Test-Path -LiteralPath $sessionOutput) -or -not (Test-Path -LiteralPath $primaryOutput)) {
		throw (New-NextPlanStateBlocker 'The session or primary WorktreeCli Output is missing.')
	}
	$sessionOutputItem = Get-Item -LiteralPath $sessionOutput -Force -ErrorAction Stop
	$primaryOutputItem = Get-Item -LiteralPath $primaryOutput -Force -ErrorAction Stop
	if (-not $sessionOutputItem.PSIsContainer -or ($sessionOutputItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
		throw (New-NextPlanStateBlocker "Session WorktreeCli Output is not a directory reparse point: '$sessionOutput'.")
	}
	if (-not $primaryOutputItem.PSIsContainer -or ($primaryOutputItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
		throw (New-NextPlanStateBlocker "Primary WorktreeCli Output is not an ordinary directory: '$primaryOutput'.")
	}
	$resolvedSessionOutput = Get-FinalizeExistingWindowsIdentity $sessionOutput 'Session WorktreeCli Output'
	$resolvedPrimaryOutput = Get-FinalizeExistingWindowsIdentity $primaryOutput 'Primary WorktreeCli Output'
	if (-not $resolvedSessionOutput.Equals($resolvedPrimaryOutput, [StringComparison]::OrdinalIgnoreCase)) {
		throw (New-NextPlanStateBlocker "Session WorktreeCli Output targets '$resolvedSessionOutput'; expected '$resolvedPrimaryOutput'.")
	}
	$worktreeCli = Join-Path $sessionOutput 'WorktreeCli.exe'
	if (-not (Test-Path -LiteralPath $worktreeCli -PathType Leaf)) {
		throw (New-NextPlanStateBlocker "Provisioned WorktreeCli is missing: '$worktreeCli'.")
	}
	$worktreeCliItem = Get-Item -LiteralPath $worktreeCli -Force -ErrorAction Stop
	if ($worktreeCliItem.PSIsContainer -or ($worktreeCliItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $worktreeCliItem.Length -eq 0) {
		throw (New-NextPlanStateBlocker "Provisioned WorktreeCli must be a nonempty ordinary file: '$worktreeCli'.")
	}
	$helpResponse = Invoke-FinalizeNativeText $worktreeCli @('--help') $worktree.Worktree
	if ($helpResponse.ExitCode -ne 0) {
		throw (New-NextPlanStateBlocker 'Provisioned WorktreeCli does not support --help.')
	}
	foreach ($capability in @(
		'WorktreeCli.exe plan order validate --repo COMMON-DIR --worktree CHECKOUT',
		'WorktreeCli.exe plan order claim-next --repo COMMON-DIR --primary-worktree CHECKOUT --worktree CHECKOUT --branch TARGET --owner TOKEN --session TOKEN --queue <plans|features>',
		'WorktreeCli.exe plan order complete --repo COMMON-DIR --worktree CHECKOUT --owner TOKEN --session TOKEN --plan PATH [--reapply]'
	)) {
		if (-not $helpResponse.Stdout.Contains($capability, [StringComparison]::Ordinal)) {
			throw (New-NextPlanStateBlocker "Provisioned WorktreeCli help is missing '$capability'.")
		}
	}

	return [pscustomobject]@{
		Worktree = $worktree.Worktree
		Primary = $primary.Worktree
		CommonDirectory = $worktree.CommonDirectory
		SessionBranch = $sessionBranch
		TargetBranch = $targetBranch
		Baseline = $baseline
		Owner = $owner
		Session = $owner
		WorktreeCli = (Get-Item -LiteralPath $worktreeCli -Force).FullName
	}
	}
	catch {
		if (Test-NextPlanStateBlocker $_) { throw $_.Exception }
		throw (New-NextPlanStateBlocker $_.Exception.Message)
	}
}

function Invoke-NextPlanProcess([string] $Executable, [string[]] $Arguments, [string] $WorkingDirectory) {
	return Invoke-FinalizeNativeText -Executable $Executable -Arguments $Arguments -WorkingDirectory $WorkingDirectory
}

function ConvertFrom-NextPlanProcessJson($Response, [string] $Operation) {
	if ([string]::IsNullOrWhiteSpace($Response.Stdout)) { throw "$Operation returned empty stdout. $($Response.Stderr.Trim())" }
	try { return $Response.Stdout.Trim() | ConvertFrom-Json -Depth 100 -ErrorAction Stop }
	catch { throw "$Operation did not return one JSON value. $($_.Exception.Message)" }
}

function Get-NextPlanSha256([byte[]] $Bytes) {
	return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}

function Get-NextPlanFileSha256([string] $Path) {
	return Get-NextPlanSha256 ([IO.File]::ReadAllBytes($Path))
}

function Write-NextPlanArtifact([string] $Worktree, [string] $Purpose, [ValidateSet('md', 'json')][string] $Extension, [byte[]] $Bytes) {
	$path = New-AgentArtifactPath -Worktree $Worktree -Purpose $Purpose -Extension $Extension
	if (Test-Path -LiteralPath $path) { throw "Artifact already exists: '$path'." }
	$temp = Join-Path ([IO.Path]::GetDirectoryName($path)) ('.' + [IO.Path]::GetFileName($path) + '.' + [guid]::NewGuid().ToString('N') + '.tmp')
	try {
		[IO.File]::WriteAllBytes($temp, $Bytes)
		[IO.File]::Move($temp, $path)
	}
	finally {
		if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Force }
	}
	return [pscustomobject]@{ Path = $path; Sha256 = Get-NextPlanSha256 $Bytes; Bytes = $Bytes.Length }
}

function Write-NextPlanJsonArtifact([string] $Worktree, [string] $Purpose, $Value) {
	$text = ($Value | ConvertTo-Json -Depth 100 -Compress) + "`n"
	return Write-NextPlanArtifact -Worktree $Worktree -Purpose $Purpose -Extension 'json' -Bytes $script:NextPlanUtf8.GetBytes($text)
}

function Read-NextPlanJsonArtifact([string] $Worktree, [string] $Path, [string] $ExpectedSha256, [string] $ExpectedSchema) {
	if ($ExpectedSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Expected artifact SHA-256 is malformed.' }
	$canonical = Assert-AgentArtifactPath -Worktree $Worktree -Path $Path
	if (-not (Test-Path -LiteralPath $canonical -PathType Leaf)) { throw "Artifact does not exist: '$canonical'." }
	$bytes = [IO.File]::ReadAllBytes($canonical)
	$actual = Get-NextPlanSha256 $bytes
	if ($actual -cne $ExpectedSha256) { throw "Artifact SHA-256 mismatch: expected $ExpectedSha256, actual $actual." }
	try { $value = $script:NextPlanUtf8.GetString($bytes) | ConvertFrom-Json -Depth 100 -ErrorAction Stop }
	catch { throw "Artifact is not strict UTF-8 JSON: $($_.Exception.Message)" }
	if ($value.schemaVersion -cne $ExpectedSchema) { throw "Artifact schema mismatch: expected '$ExpectedSchema'." }
	return [pscustomobject]@{ Path = $canonical; Sha256 = $actual; Value = $value }
}

function Read-NextPlanArtifact([string] $Worktree, [string] $Path, [string] $ExpectedSha256) {
	if ($ExpectedSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Expected artifact SHA-256 is malformed.' }
	$canonical = Assert-AgentArtifactPath -Worktree $Worktree -Path $Path
	if (-not (Test-Path -LiteralPath $canonical -PathType Leaf)) { throw "Artifact does not exist: '$canonical'." }
	$bytes = [IO.File]::ReadAllBytes($canonical)
	$actual = Get-NextPlanSha256 $bytes
	if ($actual -cne $ExpectedSha256) { throw "Artifact SHA-256 mismatch: expected $ExpectedSha256, actual $actual." }
	return [pscustomobject]@{ Path = $canonical; Sha256 = $actual; Bytes = $bytes }
}

function Assert-NextPlanRepositoryPath([string] $Worktree, [string] $Path, [string] $Label) {
	$full = Get-FinalizeRootPreservingFullPath $Path
	$relative = [IO.Path]::GetRelativePath($Worktree, $full)
	if ([IO.Path]::IsPathRooted($relative) -or $relative -eq '..' -or $relative.StartsWith("..$([IO.Path]::DirectorySeparatorChar)", [StringComparison]::Ordinal)) {
		throw "$Label must be contained by the session worktree."
	}
	$existing = Get-FinalizeExistingWindowsIdentity $full $Label
	if (-not $existing.Equals($full, [StringComparison]::OrdinalIgnoreCase)) { throw "$Label uses a reparse point." }
	return $full
}

function Assert-NextPlanTempPath([string] $Worktree, [string] $Path, [string] $Label) {
	$full = Assert-NextPlanRepositoryPath $Worktree $Path $Label
	$temp = Assert-NextPlanRepositoryPath $Worktree (Join-Path $Worktree 'Temp') 'Worktree Temp directory'
	$relative = [IO.Path]::GetRelativePath($temp, $full)
	if ([IO.Path]::IsPathRooted($relative) -or $relative -eq '..' -or
		$relative.StartsWith("..$([IO.Path]::DirectorySeparatorChar)", [StringComparison]::Ordinal)) {
		throw "$Label must be contained by the worktree Temp directory."
	}
	return $full
}

function Assert-NextPlanGitPath([string] $Path) {
	Assert-FinalizeGitPath $Path
}

function Get-NextPlanRowIdentity([string] $Order, [string] $Plan) {
	$orderDirectory = [IO.Path]::GetDirectoryName($Order.Replace('/', [IO.Path]::DirectorySeparatorChar))
	$rowPlan = [IO.Path]::GetRelativePath($orderDirectory, $Plan.Replace('/', [IO.Path]::DirectorySeparatorChar)).Replace('\', '/')
	Assert-FinalizeGitPath $Order
	Assert-FinalizeGitPath $Plan
	Assert-FinalizeGitPath $rowPlan
	return $rowPlan
}

function Test-NextPlanOnlyAllowedPreCodeChanges([string] $Worktree, [string] $Baseline, [string] $Plan) {
	$changed = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($arguments in @(
		@('diff', '--name-only', '--no-renames', '-z', $Baseline, '--'),
		@('ls-files', '--others', '--exclude-standard', '-z')
	)) {
		foreach ($path in (Invoke-FinalizeGit $Worktree $arguments).Split([char]0, [StringSplitOptions]::RemoveEmptyEntries)) {
			[void]$changed.Add($path.Replace('\', '/'))
		}
	}
	$unexpected = @($changed | Where-Object { $_ -cne $Plan } | Sort-Object)
	return [pscustomobject]@{ Allowed = $unexpected.Count -eq 0; Changed = @($changed | Sort-Object); Unexpected = $unexpected }
}

function Get-NextPlanPresentationRanges([string[]] $Lines) {
	$ranges = [Collections.Generic.List[string]]::new()
	if ($Lines.Count -eq 0) { throw 'Presentation must contain at least one line.' }
	$start = 1
	$bytes = 0
	for ($index = 0; $index -lt $Lines.Count; ++$index) {
		$lineBytes = $script:NextPlanUtf8.GetByteCount($Lines[$index] + "`n")
		if ($lineBytes -gt 16384) { throw "Presentation line $($index + 1) exceeds 16 KiB." }
		if ($bytes -ne 0 -and ($bytes + $lineBytes) -gt 16384) {
			$ranges.Add("L$start-L$index")
			$start = $index + 1
			$bytes = 0
		}
		$bytes += $lineBytes
	}
	$ranges.Add("L$start-L$($Lines.Count)")
	return $ranges.ToArray()
}

function Get-NextPlanPresentationRangesFromBytes([byte[]] $Bytes) {
	if ($Bytes.Length -eq 0 -or $Bytes[$Bytes.Length - 1] -ne 10) { throw 'Presentation must end with LF.' }
	try { $text = $script:NextPlanUtf8.GetString($Bytes) }
	catch [Text.DecoderFallbackException] { throw 'Presentation is not strict UTF-8.' }
	if ($text.Contains("`r", [StringComparison]::Ordinal)) { throw 'Presentation must use LF line endings.' }
	$body = $text.Substring(0, $text.Length - 1)
	return Get-NextPlanPresentationRanges $body.Split("`n")
}

function Get-NextPlanManifest([string] $Worktree, [string] $Baseline) {
	$rows = @(Get-FinalizeManifestRows $Worktree $Baseline)
	return [pscustomobject]@{ Rows = $rows; Sha256 = Get-FinalizeManifestSha256 $rows }
}

Export-ModuleMember -Function New-NextPlanStateBlocker,Test-NextPlanStateBlocker,Get-NextPlanContext,Invoke-NextPlanProcess,ConvertFrom-NextPlanProcessJson,Get-NextPlanSha256,Get-NextPlanFileSha256,Write-NextPlanArtifact,Write-NextPlanJsonArtifact,Read-NextPlanJsonArtifact,Read-NextPlanArtifact,Assert-NextPlanRepositoryPath,Assert-NextPlanTempPath,Assert-NextPlanGitPath,Get-NextPlanRowIdentity,Test-NextPlanOnlyAllowedPreCodeChanges,Get-NextPlanPresentationRanges,Get-NextPlanPresentationRangesFromBytes,Get-NextPlanManifest

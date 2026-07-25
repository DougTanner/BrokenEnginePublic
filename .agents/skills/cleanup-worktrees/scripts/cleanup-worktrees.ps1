[CmdletBinding()]
param(
	[switch] $Preview
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-CanonicalPath {
	param([Parameter(Mandatory = $true)][string] $Path)
	return [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
}

function Invoke-Git {
	param([Parameter(Mandatory = $true)][string[]] $Arguments)

	$output = @(& git @Arguments 2>&1 | ForEach-Object { $_.ToString() })
	return [pscustomobject] @{
		ExitCode = $LASTEXITCODE
		Output = $output
	}
}

function Test-PathUnderRoot {
	param(
		[Parameter(Mandatory = $true)][string] $Path,
		[Parameter(Mandatory = $true)][string] $Root
	)

	$prefix = $Root + [IO.Path]::DirectorySeparatorChar
	return $Path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Get-WorktreeRecords {
	param([Parameter(Mandatory = $true)][string] $RepositoryRoot)

	$result = Invoke-Git -Arguments @('-C', $RepositoryRoot, 'worktree', 'list', '--porcelain')
	if ($result.ExitCode -ne 0) {
		throw "git worktree list failed: $($result.Output -join '; ')"
	}

	$records = @()
	$record = $null
	foreach ($line in $result.Output) {
		if ($line -like 'worktree *') {
			if ($null -ne $record) { $records += [pscustomobject] $record }
			$record = @{
				Path = $line.Substring(9)
				Branch = ''
				IsPrunable = $false
				PrunableReason = ''
			}
		}
		elseif ($line -like 'branch *') { $record.Branch = $line.Substring(7) -replace '^refs/heads/', '' }
		elseif ($line -like 'prunable*') {
			$record.IsPrunable = $true
			$record.PrunableReason = $line.Substring(8).Trim()
		}
	}
	if ($null -ne $record) { $records += [pscustomobject] $record }
	return $records
}

function Get-PrimaryState {
	param([Parameter(Mandatory = $true)][string] $RepositoryRoot)

	$branchResult = Invoke-Git -Arguments @('-C', $RepositoryRoot, 'branch', '--show-current')
	$headResult = Invoke-Git -Arguments @('-C', $RepositoryRoot, 'rev-parse', 'HEAD')
	return [pscustomobject] @{
		Branch = if ($branchResult.ExitCode -eq 0 -and $branchResult.Output.Count -eq 1) { $branchResult.Output[0].Trim() } else { '' }
		Head = if ($headResult.ExitCode -eq 0 -and $headResult.Output.Count -eq 1) { $headResult.Output[0].Trim() } else { '' }
	}
}

function Add-Retained {
	param(
		[Parameter(Mandatory = $true)][AllowEmptyCollection()][Collections.Generic.List[object]] $List,
		[Parameter(Mandatory = $true)][string] $Path,
		[Parameter(Mandatory = $true)][string] $Reason,
		[bool] $Expected = $false
	)

	$List.Add([pscustomobject] @{ Path = $Path; Reason = $Reason; Expected = $Expected })
}

function Write-Report {
	param(
		[Parameter(Mandatory = $true)][string] $Status,
		[Parameter(Mandatory = $true)][string] $RepositoryRoot,
		[Parameter(Mandatory = $true)][string] $PrimaryBranch,
		[Parameter(Mandatory = $true)][string] $PrimaryHead,
		[Parameter(Mandatory = $true)][AllowEmptyCollection()][Collections.Generic.List[object]] $Removed,
		[Parameter(Mandatory = $true)][AllowEmptyCollection()][Collections.Generic.List[object]] $Retained,
		[Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]] $SnapshotRefs,
		[Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]] $Residuals
	)

	Write-Output "Cleanup: $Status"
	Write-Output 'Primary:'
	Write-Output "- Checkout: $RepositoryRoot"
	Write-Output "- Branch: $PrimaryBranch"
	Write-Output "- HEAD: $PrimaryHead"
	Write-Output 'Removed:'
	if ($Removed.Count -eq 0) { Write-Output '- none' }
	else {
		foreach ($item in $Removed) { Write-Output "- $($item.Path) | branch $($item.Branch): $($item.BranchStatus)" }
	}
	Write-Output 'Retained:'
	if ($Retained.Count -eq 0) { Write-Output '- none' }
	else {
		foreach ($item in $Retained) { Write-Output "- $($item.Path) | $($item.Reason)" }
	}
	Write-Output 'Snapshot refs:'
	if ($SnapshotRefs.Count -eq 0) { Write-Output '- none' }
	else {
		foreach ($item in $SnapshotRefs) { Write-Output "- $($item.Ref) | $($item.Object) | $($item.Date)" }
	}
	if ($Residuals.Count -eq 0) { Write-Output 'Residuals: none' }
	else { Write-Output "Residuals: $($Residuals -join '; ')" }
}

$removed = [Collections.Generic.List[object]]::new()
$retained = [Collections.Generic.List[object]]::new()
$residuals = [Collections.Generic.List[string]]::new()
$snapshotRefs = @()
$repositoryRoot = ''
$primaryBranch = '<unproven>'
$primaryHead = '<unproven>'

try {
	$rootResult = Invoke-Git -Arguments @('rev-parse', '--show-toplevel')
	if ($rootResult.ExitCode -ne 0 -or $rootResult.Output.Count -ne 1) { throw 'cleanup-worktrees must run inside a Git repository' }
	$repositoryRoot = Get-CanonicalPath -Path $rootResult.Output[0].Trim()
	$repositoryName = Split-Path -Leaf $repositoryRoot

	$gitDirResult = Invoke-Git -Arguments @('-C', $repositoryRoot, 'rev-parse', '--path-format=absolute', '--git-dir')
	$commonDirResult = Invoke-Git -Arguments @('-C', $repositoryRoot, 'rev-parse', '--path-format=absolute', '--git-common-dir')
	if ($gitDirResult.ExitCode -ne 0 -or $commonDirResult.ExitCode -ne 0) { throw 'unable to resolve Git directories' }
	$gitDir = Get-CanonicalPath -Path $gitDirResult.Output[0].Trim()
	$commonDir = Get-CanonicalPath -Path $commonDirResult.Output[0].Trim()
	if (-not $gitDir.Equals($commonDir, [StringComparison]::OrdinalIgnoreCase)) { throw 'cleanup-worktrees must run from the primary checkout' }

	$initialPrimary = Get-PrimaryState -RepositoryRoot $repositoryRoot
	$primaryBranch = $initialPrimary.Branch
	$primaryHead = $initialPrimary.Head
	if ([string]::IsNullOrWhiteSpace($primaryBranch) -or [string]::IsNullOrWhiteSpace($primaryHead)) { throw 'primary checkout must have an attached branch and readable HEAD' }

	$snapshotResult = Invoke-Git -Arguments @('-C', $repositoryRoot, 'for-each-ref', 'refs/codex/snapshots', '--format=%(refname)%09%(objectname)%09%(creatordate:iso-strict)')
	if ($snapshotResult.ExitCode -ne 0) { $residuals.Add('unable to enumerate Codex snapshot refs') }
	else {
		foreach ($line in $snapshotResult.Output) {
			$parts = $line -split "`t", 3
			if ($parts.Count -eq 3) { $snapshotRefs += [pscustomobject] @{ Ref = $parts[0]; Object = $parts[1]; Date = $parts[2] } }
		}
	}

	$claudeRoot = Get-CanonicalPath -Path (Join-Path $HOME ".claude/worktrees/$repositoryName")
	$codexRoot = Get-CanonicalPath -Path (Join-Path $HOME ".codex/worktrees/$repositoryName")
	$cutoff = [DateTime]::Now.AddHours(-48)
	$eligible = @()
	foreach ($record in @(Get-WorktreeRecords -RepositoryRoot $repositoryRoot)) {
		$path = Get-CanonicalPath -Path $record.Path
		$expectedPrefix = ''
		if (Test-PathUnderRoot -Path $path -Root $claudeRoot) { $expectedPrefix = 'claude/' }
		elseif (Test-PathUnderRoot -Path $path -Root $codexRoot) { $expectedPrefix = 'codex/' }
		else { continue }

		if ($record.IsPrunable) { Add-Retained -List $retained -Path $path -Reason "prunable: $($record.PrunableReason)"; continue }
		if (-not (Test-Path -LiteralPath $path)) { Add-Retained -List $retained -Path $path -Reason 'path is missing'; continue }
		$created = (Get-Item -LiteralPath $path -Force).CreationTime
		if ($created -gt $cutoff) { Add-Retained -List $retained -Path $path -Reason 'younger than 48 hours' -Expected $true; continue }
		if ($Preview) { Add-Retained -List $retained -Path $path -Reason 'eligible (preview)' -Expected $true }
		else { $eligible += [pscustomobject] @{ Path = $path; Prefix = $expectedPrefix } }
	}

	foreach ($candidate in $eligible) {
		$currentRecords = @(Get-WorktreeRecords -RepositoryRoot $repositoryRoot | Where-Object {
			(Get-CanonicalPath -Path $_.Path).Equals($candidate.Path, [StringComparison]::OrdinalIgnoreCase)
		})
		if ($currentRecords.Count -ne 1) { Add-Retained -List $retained -Path $candidate.Path -Reason 'registration changed before removal'; continue }
		$current = $currentRecords[0]
		if (-not (Test-Path -LiteralPath $candidate.Path)) { Add-Retained -List $retained -Path $candidate.Path -Reason 'path is missing before removal'; continue }
		if ((Get-Item -LiteralPath $candidate.Path -Force).CreationTime -gt $cutoff) { Add-Retained -List $retained -Path $candidate.Path -Reason 'younger than 48 hours before removal'; continue }

		$removeResult = Invoke-Git -Arguments @('-c', 'core.longpaths=true', '-C', $repositoryRoot, 'worktree', 'remove', '--force', $candidate.Path)
		if ($removeResult.ExitCode -ne 0) {
			Add-Retained -List $retained -Path $candidate.Path -Reason "git worktree remove failed: $($removeResult.Output -join '; ')"
			continue
		}
		if ([string]::IsNullOrWhiteSpace($current.Branch)) {
			$removed.Add([pscustomobject] @{ Path = $candidate.Path; Branch = '<none>'; BranchStatus = 'no branch to delete' })
			continue
		}
		if (-not $current.Branch.StartsWith($candidate.Prefix, [StringComparison]::Ordinal)) {
			$removed.Add([pscustomobject] @{ Path = $candidate.Path; Branch = $current.Branch; BranchStatus = 'left (unexpected prefix)' })
			continue
		}
		$removedItem = [pscustomobject] @{ Path = $candidate.Path; Branch = $current.Branch; BranchStatus = 'retained' }
		$removed.Add($removedItem)
		$branchResult = Invoke-Git -Arguments @('-C', $repositoryRoot, 'branch', '-D', '--', $current.Branch)
		if ($branchResult.ExitCode -ne 0) {
			$residuals.Add("branch deletion failed for $($current.Branch)")
			continue
		}
		$removedItem.BranchStatus = 'deleted'
	}

	$unexpectedRetained = @($retained | Where-Object { -not $_.Expected })
	if ($Preview) { $cleanupStatus = 'PREVIEW' }
	elseif ($residuals.Count -ne 0 -or $unexpectedRetained.Count -ne 0) { $cleanupStatus = 'PARTIAL' }
	else { $cleanupStatus = 'COMPLETED' }
	if ($unexpectedRetained.Count -ne 0) { $residuals.Add("$($unexpectedRetained.Count) worktree(s) retained for safety; see Retained") }
	Write-Report -Status $cleanupStatus -RepositoryRoot $repositoryRoot -PrimaryBranch $primaryBranch -PrimaryHead $primaryHead -Removed $removed -Retained $retained -SnapshotRefs $snapshotRefs -Residuals $residuals.ToArray()
	if ($cleanupStatus -eq 'PARTIAL') { exit 2 }
}
catch {
	$residuals.Add($_.Exception.Message)
	Write-Report -Status 'BLOCKED' -RepositoryRoot $(if ($repositoryRoot) { $repositoryRoot } else { '<unproven>' }) -PrimaryBranch $primaryBranch -PrimaryHead $primaryHead -Removed $removed -Retained $retained -SnapshotRefs $snapshotRefs -Residuals $residuals.ToArray()
	exit 1
}

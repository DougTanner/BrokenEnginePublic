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
				Head = ''
				Branch = ''
				IsLocked = $false
				LockReason = ''
				IsPrunable = $false
				PrunableReason = ''
				Detached = $false
			}
		}
		elseif ($line -like 'HEAD *') { $record.Head = $line.Substring(5) }
		elseif ($line -like 'branch *') { $record.Branch = $line.Substring(7) -replace '^refs/heads/', '' }
		elseif ($line -eq 'detached') { $record.Detached = $true }
		elseif ($line -like 'locked*') {
			$record.IsLocked = $true
			$record.LockReason = $line.Substring(6).Trim()
		}
		elseif ($line -like 'prunable*') {
			$record.IsPrunable = $true
			$record.PrunableReason = $line.Substring(8).Trim()
		}
	}
	if ($null -ne $record) { $records += [pscustomobject] $record }
	return $records
}

function Get-GitOperation {
	param([Parameter(Mandatory = $true)][string] $WorktreePath)

	foreach ($marker in @(
		'MERGE_HEAD',
		'rebase-merge',
		'rebase-apply',
		'CHERRY_PICK_HEAD',
		'REVERT_HEAD',
		'BISECT_LOG',
		'sequencer'
	)) {
		$result = Invoke-Git -Arguments @('-C', $WorktreePath, 'rev-parse', '--git-path', $marker)
		if ($result.ExitCode -ne 0 -or $result.Output.Count -ne 1) { return "unreadable operation marker $marker" }
		$markerPath = $result.Output[0]
		if (-not [IO.Path]::IsPathRooted($markerPath)) { $markerPath = Join-Path $WorktreePath $markerPath }
		if (Test-Path -LiteralPath $markerPath) { return $marker }
	}
	return ''
}

function Get-PrimaryState {
	param([Parameter(Mandatory = $true)][string] $RepositoryRoot)

	$branchResult = Invoke-Git -Arguments @('-C', $RepositoryRoot, 'branch', '--show-current')
	$headResult = Invoke-Git -Arguments @('-C', $RepositoryRoot, 'rev-parse', 'HEAD')
	$statusResult = Invoke-Git -Arguments @('-C', $RepositoryRoot, 'status', '--porcelain=v1', '--untracked-files=all')
	$operation = Get-GitOperation -WorktreePath $RepositoryRoot
	return [pscustomobject] @{
		Branch = if ($branchResult.ExitCode -eq 0 -and $branchResult.Output.Count -eq 1) { $branchResult.Output[0].Trim() } else { '' }
		Head = if ($headResult.ExitCode -eq 0 -and $headResult.Output.Count -eq 1) { $headResult.Output[0].Trim() } else { '' }
		StatusReadable = $statusResult.ExitCode -eq 0
		Dirty = $statusResult.Output.Count -ne 0
		Operation = $operation
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
	if (-not $Preview) {
		if (-not $initialPrimary.StatusReadable) { throw 'primary status is unreadable' }
		if ($initialPrimary.Dirty) { throw 'primary checkout is dirty' }
		if ($initialPrimary.Operation) { throw "primary checkout has Git operation $($initialPrimary.Operation)" }
	}

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
	$today = [DateTime]::Today
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
		if ($created -ge $today) { Add-Retained -List $retained -Path $path -Reason 'created today' -Expected $true; continue }
		if ($record.Detached -or [string]::IsNullOrWhiteSpace($record.Branch)) { Add-Retained -List $retained -Path $path -Reason 'detached or missing branch'; continue }
		if (-not $record.Branch.StartsWith($expectedPrefix, [StringComparison]::Ordinal)) { Add-Retained -List $retained -Path $path -Reason "unexpected branch $($record.Branch)"; continue }
		if ($record.IsLocked) { Add-Retained -List $retained -Path $path -Reason "locked: $($record.LockReason)"; continue }
		$operation = Get-GitOperation -WorktreePath $path
		if ($operation) { Add-Retained -List $retained -Path $path -Reason "Git operation $operation"; continue }
		$statusResult = Invoke-Git -Arguments @('-c', 'core.longpaths=true', '-C', $path, 'status', '--porcelain=v1', '--untracked-files=all', '--ignore-submodules=none')
		if ($statusResult.ExitCode -ne 0) { Add-Retained -List $retained -Path $path -Reason 'status is unreadable'; continue }
		if ($statusResult.Output.Count -ne 0) { Add-Retained -List $retained -Path $path -Reason "dirty: $($statusResult.Output -join '; ')"; continue }
		$ancestorResult = Invoke-Git -Arguments @('-C', $repositoryRoot, 'merge-base', '--is-ancestor', $record.Head, $primaryHead)
		if ($ancestorResult.ExitCode -ne 0) { Add-Retained -List $retained -Path $path -Reason 'tip is not contained in primary'; continue }
		if ($Preview) { Add-Retained -List $retained -Path $path -Reason 'eligible (preview)' -Expected $true }
		else { $eligible += [pscustomobject] @{ Record = $record; Path = $path; Created = $created; Prefix = $expectedPrefix } }
	}

	foreach ($candidate in $eligible) {
		$currentPrimary = Get-PrimaryState -RepositoryRoot $repositoryRoot
		if ($currentPrimary.Branch -ne $primaryBranch -or $currentPrimary.Head -ne $primaryHead -or -not $currentPrimary.StatusReadable -or $currentPrimary.Dirty -or $currentPrimary.Operation) {
			$residuals.Add('primary checkout changed during cleanup; remaining candidates were not touched')
			break
		}

		$currentRecords = @(Get-WorktreeRecords -RepositoryRoot $repositoryRoot | Where-Object {
			(Get-CanonicalPath -Path $_.Path).Equals($candidate.Path, [StringComparison]::OrdinalIgnoreCase)
		})
		if ($currentRecords.Count -ne 1) { Add-Retained -List $retained -Path $candidate.Path -Reason 'registration changed before removal'; continue }
		$current = $currentRecords[0]
		if ($current.Head -ne $candidate.Record.Head -or $current.Branch -ne $candidate.Record.Branch -or $current.Detached -or $current.IsPrunable -or $current.IsLocked) {
			Add-Retained -List $retained -Path $candidate.Path -Reason 'identity or lock state changed before removal'
			continue
		}
		if (-not (Test-Path -LiteralPath $candidate.Path) -or (Get-Item -LiteralPath $candidate.Path -Force).CreationTime -ge $today) {
			Add-Retained -List $retained -Path $candidate.Path -Reason 'path or creation date changed before removal'
			continue
		}
		$operation = Get-GitOperation -WorktreePath $candidate.Path
		$statusResult = Invoke-Git -Arguments @('-c', 'core.longpaths=true', '-C', $candidate.Path, 'status', '--porcelain=v1', '--untracked-files=all', '--ignore-submodules=none')
		$ancestorResult = Invoke-Git -Arguments @('-C', $repositoryRoot, 'merge-base', '--is-ancestor', $current.Head, $primaryHead)
		if ($operation -or $statusResult.ExitCode -ne 0 -or $statusResult.Output.Count -ne 0 -or $ancestorResult.ExitCode -ne 0) {
			Add-Retained -List $retained -Path $candidate.Path -Reason 'safety state changed before removal'
			continue
		}
		$finalHeadResult = Invoke-Git -Arguments @('-C', $candidate.Path, 'rev-parse', 'HEAD')
		$finalBranchResult = Invoke-Git -Arguments @('-C', $candidate.Path, 'branch', '--show-current')
		if (
			$finalHeadResult.ExitCode -ne 0 -or
			$finalHeadResult.Output.Count -ne 1 -or
			$finalHeadResult.Output[0].Trim() -ne $current.Head -or
			$finalBranchResult.ExitCode -ne 0 -or
			$finalBranchResult.Output.Count -ne 1 -or
			$finalBranchResult.Output[0].Trim() -ne $current.Branch
		) {
			Add-Retained -List $retained -Path $candidate.Path -Reason 'HEAD or branch changed immediately before removal'
			continue
		}

		$removeResult = Invoke-Git -Arguments @('-c', 'core.longpaths=true', '-C', $repositoryRoot, 'worktree', 'remove', $candidate.Path)
		if ($removeResult.ExitCode -ne 0) {
			Add-Retained -List $retained -Path $candidate.Path -Reason "git worktree remove failed: $($removeResult.Output -join '; ')"
			continue
		}
		$removedItem = [pscustomobject] @{ Path = $candidate.Path; Branch = $current.Branch; BranchStatus = 'retained' }
		$removed.Add($removedItem)
		$branchResult = Invoke-Git -Arguments @('-C', $repositoryRoot, 'branch', '-d', '--', $current.Branch)
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

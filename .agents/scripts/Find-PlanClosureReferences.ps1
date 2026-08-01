[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $Worktree,
	[Parameter(Mandatory)][string] $Baseline,
	[Parameter(Mandatory)][string] $CompletedPlan
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$commonModule = Join-Path $PSScriptRoot 'FinalizeWorkflowCommon.psm1'
Import-Module $commonModule -Force

$identity = Get-FinalizeGitIdentity $Worktree 'Plan closure worktree'
if ($Baseline -cnotmatch '^[0-9a-f]{40}$' -or -not (Test-FinalizeGitSuccess $identity.Worktree @('rev-parse', '--verify', "$Baseline^{commit}"))) {
	throw "Baseline is not a commit in this repository: '$Baseline'."
}
$plan = $CompletedPlan.Replace('\', '/')
if ($plan.StartsWith('./', [StringComparison]::Ordinal)) { $plan = $plan.Substring(2) }
if (-not $plan.StartsWith('Documents/Plans/', [StringComparison]::Ordinal) -and -not $plan.StartsWith('Documents/Features/', [StringComparison]::Ordinal)) {
	throw "CompletedPlan must be a canonical plan path: '$CompletedPlan'."
}
$terms = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
[void]$terms.Add($plan)
[void]$terms.Add($plan.Substring($plan.IndexOf('/') + 1))
[void]$terms.Add([IO.Path]::GetFileName($plan))
[void]$terms.Add([IO.Path]::GetFileNameWithoutExtension($plan))

$renameOutput = Invoke-FinalizeGit $identity.Worktree @('diff', '--name-status', '--find-renames', $Baseline, '--')
foreach ($line in $renameOutput -split "`r?`n") {
	$columns = $line -split "`t"
	if ($columns.Count -ge 3 -and ($columns[0].StartsWith('R', [StringComparison]::Ordinal) -or $columns[0].StartsWith('C', [StringComparison]::Ordinal))) {
		[void]$terms.Add($columns[1].Replace('\', '/'))
		[void]$terms.Add([IO.Path]::GetFileName($columns[1]))
	}
}

$hits = [Collections.Generic.List[object]]::new()
foreach ($root in @('Documents/Plans', 'Documents/Features', 'Documents/Investigations')) {
	$absoluteRoot = Join-Path $identity.Worktree $root
	if (-not (Test-Path -LiteralPath $absoluteRoot -PathType Container)) { continue }
	foreach ($file in Get-ChildItem -LiteralPath $absoluteRoot -Recurse -File -Filter '*.md') {
		$relative = [IO.Path]::GetRelativePath($identity.Worktree, $file.FullName).Replace('\', '/')
		if ($relative.Equals($plan, [StringComparison]::OrdinalIgnoreCase)) { continue }
		$lineNumber = 0
		foreach ($line in [IO.File]::ReadLines($file.FullName)) {
			++$lineNumber
			foreach ($term in $terms) {
				if ($line.IndexOf($term, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
					$hits.Add([pscustomobject]@{ path = $relative; line = $lineNumber; term = $term; text = $line.Trim() })
					break
				}
			}
		}
	}
}
[pscustomobject]@{ schema = 'broken-engine-plan-closure-references/v1'; baseline = $Baseline; completedPlan = $plan; terms = @($terms | Sort-Object); hits = @($hits) } | ConvertTo-Json -Depth 8

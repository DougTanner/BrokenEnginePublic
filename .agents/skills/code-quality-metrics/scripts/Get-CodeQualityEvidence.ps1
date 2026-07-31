# Digest wrapper over the public Invoke-CodeQualityMetrics.ps1 entry point. It never computes a
# metric: it invokes the entry point unchanged and selects, orders, and counts fields the resulting
# broken-engine-code-quality-metrics/v2 report already contains, so the large corpus report never
# has to enter a review context. A field the digest reads that is missing or renamed blocks loudly
# instead of degrading the result silently.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][ValidateSet('Snapshot', 'Compare')][string] $Mode,
	[string] $Target,
	[string] $Scope,
	[string] $TargetManifest,
	[string] $Baseline,
	[string] $RepositoryRoot,
	[string] $Profile,
	[switch] $Phase0Hints,
	[string] $EvidenceDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:HintLimit = 10
$script:CloneInstanceLimit = 4

$result = [ordered]@{
	schemaVersion = 'broken-engine-code-quality-evidence/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Code quality evidence digest did not run.'
	retained = $false
	evidencePath = $null
	profile = $null
	targetSelection = $null
	coverage = $null
	comparison = $null
}

function Complete-CodeQualityEvidence([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 64 -Compress))
	exit $ExitCode
}

function Test-MetricContractField([object] $Node, [string] $Name) {
	return $null -ne $Node -and $null -ne $Node.PSObject -and ($Node.PSObject.Properties.Name -ccontains $Name)
}

function Confirm-MetricContractRowField([object] $Row, [string[]] $Names, [string] $RowPath) {
	foreach ($name in $Names) {
		if (-not (Test-MetricContractField $Row $name)) {
			Complete-CodeQualityEvidence 1 'error' 'evidence.contract-mismatch' "The metrics report does not contain the field the digest reads: $RowPath.$name."
		}
	}
}

function Get-MetricContractField([object] $Report, [string] $Path) {
	$node = $Report
	foreach ($segment in $Path.Split('.')) {
		if (-not (Test-MetricContractField $node $segment)) {
			Complete-CodeQualityEvidence 1 'error' 'evidence.contract-mismatch' "The metrics report does not contain the field the digest reads: $Path."
		}
		$node = $node.$segment
	}
	return $node
}

function Invoke-MetricEntryPoint([string] $Executable, [string[]] $Arguments) {
	$startInfo = [Diagnostics.ProcessStartInfo]::new()
	$startInfo.FileName = $Executable
	$startInfo.UseShellExecute = $false
	$startInfo.RedirectStandardOutput = $true
	$startInfo.RedirectStandardError = $true
	$startInfo.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
	$startInfo.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
	foreach ($argument in $Arguments) { [void]$startInfo.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	try {
		$process.StartInfo = $startInfo
		if (-not $process.Start()) { throw 'The code quality metrics entry point failed to start.' }
		# Both streams are drained concurrently: the report is far larger than a pipe buffer.
		$standardOutput = $process.StandardOutput.ReadToEndAsync()
		$standardError = $process.StandardError.ReadToEndAsync()
		$process.WaitForExit()
		return [pscustomobject]@{
			ExitCode = $process.ExitCode
			Stdout = $standardOutput.GetAwaiter().GetResult()
			Stderr = $standardError.GetAwaiter().GetResult()
		}
	}
	finally { $process.Dispose() }
}

function Save-EvidenceFile([string] $Directory, [string] $Root, [string] $Revision) {
	# Retention is the entry point's own -OutputPath write; this only fixes the recoverable path.
	if (-not [IO.Path]::IsPathFullyQualified($Directory)) { throw "EvidenceDirectory must be an absolute path: '$Directory'." }
	if (-not (Test-Path -LiteralPath $Directory -PathType Container)) { throw "EvidenceDirectory must be an existing directory: '$Directory'." }
	if (-not $Root) { throw 'EvidenceDirectory requires RepositoryRoot to resolve the evidence revision.' }
	Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\AgentScriptCommon.psm1') -Force -DisableNameChecking
	$shortSha = @(Invoke-AgentGit @('-C', $Root, 'rev-parse', '--short=12', $Revision))[0].Trim()
	$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ', [Globalization.CultureInfo]::InvariantCulture)
	return (Join-Path $Directory "code-quality-evidence-$stamp-$shortSha.json")
}

function Get-OrderedDigestRow([object[]] $Rows, [scriptblock] $Weight, [scriptblock] $Key) {
	$ordered = [Collections.Generic.List[object]]::new()
	foreach ($row in $Rows) {
		$ordered.Add([pscustomobject]@{ Weight = [double](& $Weight $row); Key = [string](& $Key $row); Value = $row })
	}
	$ordered.Sort([Comparison[object]] {
		param($left, $right)
		if ($left.Weight -ne $right.Weight) { return $right.Weight.CompareTo($left.Weight) }
		return [string]::CompareOrdinal($left.Key, $right.Key)
	})
	$values = [Collections.Generic.List[object]]::new()
	foreach ($entry in $ordered) { $values.Add($entry.Value) }
	return , $values
}

function New-HintCategory([object] $Ordered, [int] $Total) {
	$items = [Collections.Generic.List[object]]::new()
	foreach ($row in $Ordered) {
		if ($items.Count -ge $script:HintLimit) { break }
		$items.Add($row)
	}
	return [ordered]@{ items = @($items); total = $Total; emitted = $items.Count }
}

function Get-Phase0Hint([object] $Report) {
	$targetPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	$identities = @(Get-MetricContractField $Report 'current.targetManifest')
	for ($index = 0; $index -lt $identities.Count; ++$index) {
		Confirm-MetricContractRowField $identities[$index] @('path') "current.targetManifest[$index]"
		[void]$targetPaths.Add([string]$identities[$index].path)
	}

	$buckets = @(Get-MetricContractField $Report 'current.targetOutliers')
	$outlierTotal = 0
	$outlierEmitted = 0
	for ($index = 0; $index -lt $buckets.Count; ++$index) {
		Confirm-MetricContractRowField $buckets[$index] @('items', 'totalCount', 'truncatedCount') "current.targetOutliers[$index]"
		$outlierTotal += [int]$buckets[$index].totalCount
		$outlierEmitted += @($buckets[$index].items).Count
	}
	# The report already truncated and ordered each bucket; re-sorting it would invent an ordering.
	$outliers = [ordered]@{ items = @($buckets); total = $outlierTotal; emitted = $outlierEmitted }

	$cloneCandidates = [Collections.Generic.List[object]]::new()
	$groups = @(Get-MetricContractField $Report 'current.cloneGroups')
	for ($groupIndex = 0; $groupIndex -lt $groups.Count; ++$groupIndex) {
		$group = $groups[$groupIndex]
		Confirm-MetricContractRowField $group @('instances') "current.cloneGroups[$groupIndex]"
		$instances = @($group.instances)
		$targetInstances = [Collections.Generic.List[object]]::new()
		$externalInstances = [Collections.Generic.List[object]]::new()
		$targetSloc = 0.0
		for ($instanceIndex = 0; $instanceIndex -lt $instances.Count; ++$instanceIndex) {
			$instance = $instances[$instanceIndex]
			$instancePath = "current.cloneGroups[$groupIndex].instances[$instanceIndex]"
			Confirm-MetricContractRowField $instance @('path') $instancePath
			if (-not $targetPaths.Contains([string]$instance.path)) { $externalInstances.Add($instance); continue }
			Confirm-MetricContractRowField $instance @('sloc') $instancePath
			$targetSloc += [double]$instance.sloc
			$targetInstances.Add($instance)
		}
		if ($targetInstances.Count -eq 0) { continue }
		Confirm-MetricContractRowField $group @('groupHash') "current.cloneGroups[$groupIndex]"
		$cloneCandidates.Add([pscustomobject]@{
			GroupHash = [string]$group.groupHash
			TargetSloc = $targetSloc
			Digest = [ordered]@{
				groupHash = [string]$group.groupHash
				targetInstances = @($targetInstances | Select-Object -First $script:CloneInstanceLimit)
				externalInstances = @($externalInstances | Select-Object -First $script:CloneInstanceLimit)
			}
		})
	}
	$cloneOrdered = Get-OrderedDigestRow @($cloneCandidates) { param($row) $row.TargetSloc } { param($row) $row.GroupHash }
	$cloneDigests = [Collections.Generic.List[object]]::new()
	foreach ($candidate in $cloneOrdered) { $cloneDigests.Add($candidate.Digest) }
	$cloneGroups = New-HintCategory $cloneDigests $cloneCandidates.Count

	$functionRows = [Collections.Generic.List[object]]::new()
	$allFunctions = @(Get-MetricContractField $Report 'current.highComplexityFunctions')
	for ($index = 0; $index -lt $allFunctions.Count; ++$index) {
		$functionPath = "current.highComplexityFunctions[$index]"
		Confirm-MetricContractRowField $allFunctions[$index] @('path') $functionPath
		if (-not $targetPaths.Contains([string]$allFunctions[$index].path)) { continue }
		Confirm-MetricContractRowField $allFunctions[$index] @('mass', 'owner', 'name', 'signature') $functionPath
		$functionRows.Add($allFunctions[$index])
	}
	$functions = New-HintCategory (Get-OrderedDigestRow @($functionRows) { param($row) [double]$row.mass } { param($row) "$($row.owner)/$($row.name)/$($row.signature)" }) $functionRows.Count

	$skipRows = [Collections.Generic.List[object]]::new()
	$allSkips = @(Get-MetricContractField $Report 'current.skips')
	for ($index = 0; $index -lt $allSkips.Count; ++$index) {
		Confirm-MetricContractRowField $allSkips[$index] @('path') "current.skips[$index]"
		if ($targetPaths.Contains([string]$allSkips[$index].path)) { $skipRows.Add($allSkips[$index]) }
	}
	$skips = New-HintCategory (Get-OrderedDigestRow @($skipRows) { param($row) 0.0 } { param($row) [string]$row.path }) $skipRows.Count

	return [ordered]@{
		targetOutliers = $outliers
		cloneGroups = $cloneGroups
		highComplexityFunctions = $functions
		skips = $skips
	}
}

try {
	$entryPoint = Join-Path $PSScriptRoot 'Invoke-CodeQualityMetrics.ps1'
	if (-not (Test-Path -LiteralPath $entryPoint -PathType Leaf)) { throw "The code quality metrics entry point is missing: '$entryPoint'." }

	$outputPath = $null
	if ($EvidenceDirectory) {
		$outputPath = Save-EvidenceFile $EvidenceDirectory $RepositoryRoot $(if ($Baseline) { $Baseline } else { 'HEAD' })
	}

	$arguments = [Collections.Generic.List[string]]::new()
	$arguments.Add('-NoProfile')
	$arguments.Add('-File')
	$arguments.Add($entryPoint)
	$arguments.AddRange([string[]]@('-Mode', $Mode))
	if ($Target) { $arguments.AddRange([string[]]@('-Target', $Target)) }
	if ($Scope) { $arguments.AddRange([string[]]@('-Scope', $Scope)) }
	if ($TargetManifest) { $arguments.AddRange([string[]]@('-TargetManifest', $TargetManifest)) }
	if ($Baseline) { $arguments.AddRange([string[]]@('-Baseline', $Baseline)) }
	if ($RepositoryRoot) { $arguments.AddRange([string[]]@('-RepositoryRoot', $RepositoryRoot)) }
	if ($Profile) { $arguments.AddRange([string[]]@('-Profile', $Profile)) }
	if ($outputPath) { $arguments.AddRange([string[]]@('-OutputPath', $outputPath)) }

	$invocation = Invoke-MetricEntryPoint ([Diagnostics.Process]::GetCurrentProcess().MainModule.FileName) @($arguments)
	if ($invocation.ExitCode -ne 0) {
		# Target parse and signature extraction failures stay exactly what the entry point reported.
		$result.underlying = [string]$invocation.Stderr
		Complete-CodeQualityEvidence 2 'blocked' 'evidence.entry-point-failure' "The code quality metrics entry point failed with exit $($invocation.ExitCode)."
	}

	$report = $invocation.Stdout | ConvertFrom-Json -Depth 64
	$adapterVersion = Get-MetricContractField $report 'tool.adapterVersion'
	if ([string]$adapterVersion -cne '4') {
		Complete-CodeQualityEvidence 1 'error' 'evidence.contract-mismatch' "The metrics report has an unexpected tool.adapterVersion: '$adapterVersion' (the digest reads '4')."
	}
	$reportProfile = Get-MetricContractField $report 'profile'
	$targetSelection = Get-MetricContractField $report 'targetSelection'
	[void](Get-MetricContractField $report 'current')
	$comparison = $null
	if ($Mode -ceq 'Compare') {
		$comparison = Get-MetricContractField $report 'comparison'
		$coverage = Get-MetricContractField $report 'comparison.coverage'
	}
	else {
		# The v2 report has no top-level coverage; Snapshot coverage is the current CaptureView counts.
		$corpusCounts = Get-MetricContractField $report 'current.corpusCounts'
		$targetCounts = Get-MetricContractField $report 'current.targetCounts'
		$coverage = [ordered]@{ corpusCounts = $corpusCounts; targetCounts = $targetCounts }
	}
	$hints = $null
	if ($Phase0Hints) { $hints = Get-Phase0Hint $report }

	$result.retained = [bool]$outputPath
	$result.evidencePath = $outputPath
	$result.profile = $reportProfile
	$result.targetSelection = $targetSelection
	$result.coverage = $coverage
	$result.comparison = $comparison
	if ($Phase0Hints) { $result.hints = $hints }
	Complete-CodeQualityEvidence 0 'pass' 'ok' 'Code quality evidence digest captured.'
}
catch {
	Complete-CodeQualityEvidence 1 'error' 'internal.error' $_.Exception.Message
}

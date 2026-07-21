# Read-only certification boundary for a v2 AgentTools candidate receipt. Binds
# immutable candidate executables to an unchanged producer manifest, same-checkout
# source bytes, and every clean-filter blob at one expected commit. The
# primary-resolved ThirdParty/tinygltf/json.hpp input is certified separately
# because the superproject commit records only the ThirdParty submodule identity.
#
# Result contract: one broken-engine-agenttools-certification-result/v1 JSON
# object. Exit 0 with status pass/code ok is certified. Exit 2 is a deterministic
# candidate mismatch that requires rebuilding the candidate. Exit 1 is malformed
# invocation or unreadable repository state.
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $RepositoryRoot,
	[Parameter(Mandatory = $true)]
	[string] $WorktreeRoot,
	[Parameter(Mandatory = $true)]
	[string] $CandidateReceiptPath,
	[Parameter(Mandatory = $true)]
	[string] $CandidateReceiptSha256,
	[Parameter(Mandatory = $true)]
	[string] $ExpectedCommit
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\AgentScriptCommon.psm1') -Force

$result = [ordered]@{
	schemaVersion = 'broken-engine-agenttools-certification-result/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Candidate certification did not run.'
	receipt = $null
	expectedCommit = $ExpectedCommit
	source = $null
	executables = $null
}
$exitCode = 1

function New-CertificationFailure([string] $Code, [string] $Message) {
	$exception = [InvalidOperationException]::new($Message)
	$exception.Data['CertificationFailure'] = $true
	$exception.Data['Code'] = $Code
	return $exception
}

function Throw-CertificationFailure([string] $Code, [string] $Message) {
	throw (New-CertificationFailure $Code "$Message Rebuild the AgentTools candidate.")
}

function Get-Sha256([string] $Path) {
	return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-BytesSha256([byte[]] $Bytes) {
	return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}

function Get-TextSha256([string] $Value) {
	$bytes = [Text.UTF8Encoding]::new($false).GetBytes($Value)
	return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
}

function Get-Property($Value, [string] $Name, [string] $Context) {
	if ($null -eq $Value -or -not ($Value.PSObject.Properties.Name -ccontains $Name)) {
		Throw-CertificationFailure 'certification.receipt-schema' "Candidate receipt is missing $Context.$Name."
	}
	return $Value.$Name
}

function Get-Manifest($Snapshot, [string] $Context) {
	$digest = [string](Get-Property $Snapshot 'digest' $Context)
	$entries = @(Get-Property $Snapshot 'manifest' $Context)
	if ($digest -cnotmatch '^[0-9a-f]{64}$' -or $entries.Count -eq 0) {
		Throw-CertificationFailure 'certification.receipt-schema' "Candidate receipt $Context has an invalid digest or empty manifest."
	}
	$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	$normalized = [Collections.Generic.List[object]]::new()
	$digestLines = [Collections.Generic.List[string]]::new()
	foreach ($entry in $entries) {
		$path = [string](Get-Property $entry 'path' "$Context.manifest[]")
		$bytes = Get-Property $entry 'bytes' "$Context.manifest[$path]"
		$sha256 = [string](Get-Property $entry 'sha256' "$Context.manifest[$path]")
		$gitBlob = [string](Get-Property $entry 'gitBlob' "$Context.manifest[$path]")
		if ([string]::IsNullOrWhiteSpace($path) -or $path -cne $path.Replace('\', '/') -or
			$path.StartsWith('/', [StringComparison]::Ordinal) -or $path -match '^[A-Za-z]:' -or
			$path -match '(^|/)(\.|\.\.)(/|$)' -or $path.Contains('//') -or
			-not ($path.StartsWith('Tools/WorktreeCli/', [StringComparison]::Ordinal) -or
				$path.StartsWith('Tools/AgentHarness/', [StringComparison]::Ordinal) -or
				$path.StartsWith('Tools/ToolCommon/', [StringComparison]::Ordinal) -or
				$path -ceq 'ThirdParty/tinygltf/json.hpp') -or
			$bytes -isnot [ValueType] -or [int64]$bytes -lt 0 -or $sha256 -cnotmatch '^[0-9a-f]{64}$' -or
			$gitBlob -cnotmatch '^[0-9a-f]{40}$' -or -not $seen.Add($path)) {
			Throw-CertificationFailure 'certification.receipt-schema' "Candidate receipt has an invalid or duplicate manifest entry '$path'."
		}
		$normalized.Add([pscustomobject]@{ path = $path; bytes = [int64]$bytes; sha256 = $sha256; gitBlob = $gitBlob })
		$digestLines.Add("$path`0$([int64]$bytes)`0$sha256`0$gitBlob")
	}
	$paths = @($normalized | ForEach-Object { $_.path })
	$sortedPaths = @($paths)
	[Array]::Sort($sortedPaths, [StringComparer]::Ordinal)
	if (($paths -join "`n") -cne ($sortedPaths -join "`n")) {
		Throw-CertificationFailure 'certification.receipt-schema' "Candidate receipt $Context manifest is not sorted by ordinal path."
	}
	$computedDigest = Get-TextSha256 (($digestLines -join "`n") + "`n")
	if ($computedDigest -cne $digest) {
		Throw-CertificationFailure 'certification.receipt-schema' "Candidate receipt $Context digest does not match its manifest."
	}
	return [pscustomobject]@{ digest = $digest; manifest = $normalized.ToArray() }
}

function Assert-ReceiptFileIdentity($Identity, [string] $Context, [bool] $AllowAbsent) {
	$path = [string](Get-Property $Identity 'path' $Context)
	$present = Get-Property $Identity 'present' $Context
	$bytes = Get-Property $Identity 'bytes' $Context
	$sha256 = Get-Property $Identity 'sha256' $Context
	$lastWriteUtc = Get-Property $Identity 'lastWriteUtc' $Context
	if ([string]::IsNullOrWhiteSpace($path) -or $present -isnot [bool]) {
		Throw-CertificationFailure 'certification.receipt-schema' "Candidate receipt $Context has an invalid path or presence flag."
	}
	if ($present) {
		$parsedTimestamp = [datetime]::MinValue
		if ($bytes -isnot [ValueType] -or [int64]$bytes -lt 0 -or [string]$sha256 -cnotmatch '^[0-9a-f]{64}$' -or
			-not [datetime]::TryParse([string]$lastWriteUtc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind, [ref]$parsedTimestamp)) {
			Throw-CertificationFailure 'certification.receipt-schema' "Candidate receipt $Context has an invalid present-file identity."
		}
	}
	elseif (-not $AllowAbsent -or $null -ne $bytes -or $null -ne $sha256 -or $null -ne $lastWriteUtc) {
		Throw-CertificationFailure 'certification.receipt-schema' "Candidate receipt $Context has an invalid absent-file identity."
	}
}

function Get-CurrentToolPaths([string] $Root) {
	$listed = @(Invoke-AgentGit (@('-C', $Root, '-c', 'core.quotePath=false', 'ls-files', '--cached', '--others', '--exclude-standard', '--', 'Tools/WorktreeCli', 'Tools/AgentHarness', 'Tools/ToolCommon')))
	$paths = [Collections.Generic.List[string]]::new()
	foreach ($listedPath in $listed) {
		$path = $listedPath.Trim().Replace('\', '/')
		if (-not [string]::IsNullOrWhiteSpace($path)) { $paths.Add($path) }
	}
	$paths.Sort([StringComparer]::Ordinal)
	return $paths.ToArray()
}

try {
	if (-not [IO.Path]::IsPathRooted($RepositoryRoot) -or -not [IO.Path]::IsPathRooted($WorktreeRoot)) {
		throw 'RepositoryRoot and WorktreeRoot must be absolute.'
	}
	if ($CandidateReceiptSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'CandidateReceiptSha256 must be 64 lowercase hexadecimal characters.' }
	if ($ExpectedCommit -cnotmatch '^[0-9a-f]{40}$') { throw 'ExpectedCommit must be a full lowercase commit hash.' }
	$repository = Get-AgentCanonicalPath $RepositoryRoot
	$worktree = Get-AgentCanonicalPath $WorktreeRoot
	foreach ($entry in @(@($repository, 'RepositoryRoot'), @($worktree, 'WorktreeRoot'))) {
		$top = Get-AgentCanonicalPath (@(Invoke-AgentGit @('-C', $entry[0], 'rev-parse', '--show-toplevel'))[0].Trim())
		if (-not $top.Equals($entry[0], [StringComparison]::OrdinalIgnoreCase)) { throw "$($entry[1]) is not a checkout root: '$($entry[0])'." }
	}
	$repositoryGit = Get-Item -LiteralPath (Join-Path $repository '.git') -Force -ErrorAction Stop
	if (-not $repositoryGit.PSIsContainer -or ($repositoryGit.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'RepositoryRoot must be the primary checkout.' }
	$repositoryCommon = Get-AgentCanonicalPath (@(Invoke-AgentGit @('-C', $repository, 'rev-parse', '--path-format=absolute', '--git-common-dir'))[0].Trim())
	$worktreeCommon = Get-AgentCanonicalPath (@(Invoke-AgentGit @('-C', $worktree, 'rev-parse', '--path-format=absolute', '--git-common-dir'))[0].Trim())
	if (-not $repositoryCommon.Equals($worktreeCommon, [StringComparison]::OrdinalIgnoreCase)) { throw 'RepositoryRoot and WorktreeRoot do not identify the same repository.' }
	$resolvedCommit = (@(Invoke-AgentGit @('-C', $worktree, 'rev-parse', '--verify', "$ExpectedCommit^{commit}"))[0]).Trim()
	if ($resolvedCommit -cne $ExpectedCommit) { throw "ExpectedCommit did not resolve exactly: '$ExpectedCommit'." }

	if (-not (Test-Path -LiteralPath $CandidateReceiptPath -PathType Leaf)) {
		Throw-CertificationFailure 'certification.receipt-missing' "Candidate receipt is missing: '$CandidateReceiptPath'."
	}
	try { $receiptBytes = [IO.File]::ReadAllBytes($CandidateReceiptPath) }
	catch { Throw-CertificationFailure 'certification.receipt-missing' "Candidate receipt cannot be read: '$CandidateReceiptPath': $($_.Exception.Message)" }
	$actualReceiptSha256 = Get-BytesSha256 $receiptBytes
	if ($actualReceiptSha256 -cne $CandidateReceiptSha256) {
		Throw-CertificationFailure 'certification.receipt-identity' "Candidate receipt hash mismatch: expected $CandidateReceiptSha256, found $actualReceiptSha256."
	}
	try {
		$receiptText = [Text.UTF8Encoding]::new($false, $true).GetString($receiptBytes)
		$receipt = $receiptText | ConvertFrom-Json -Depth 100 -ErrorAction Stop
	}
	catch { Throw-CertificationFailure 'certification.receipt-schema' "Candidate receipt is not valid JSON: $($_.Exception.Message)" }
	if ([string](Get-Property $receipt 'schemaVersion' 'receipt') -cne 'broken-engine-agenttools-candidate/v2') {
		Throw-CertificationFailure 'certification.receipt-schema' 'Candidate receipt is not broken-engine-agenttools-candidate/v2.'
	}
	$createdAt = [datetime]::MinValue
	if (-not [datetime]::TryParse([string](Get-Property $receipt 'createdAt' 'receipt'), [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind, [ref]$createdAt)) {
		Throw-CertificationFailure 'certification.receipt-schema' 'Candidate receipt createdAt is invalid.'
	}
	$checkout = Get-Property $receipt 'checkout' 'receipt'
	$receiptCheckoutRoot = [string](Get-Property $checkout 'root' 'receipt.checkout')
	$receiptCommonPath = [string](Get-Property $checkout 'gitCommonDirectory' 'receipt.checkout')
	$receiptSourceCommit = [string](Get-Property $checkout 'sourceCommit' 'receipt.checkout')
	if (-not [IO.Path]::IsPathRooted($receiptCheckoutRoot) -or -not [IO.Path]::IsPathRooted($receiptCommonPath) -or
		$receiptSourceCommit -cnotmatch '^[0-9a-f]{40}$') {
		Throw-CertificationFailure 'certification.receipt-schema' 'Candidate receipt checkout identity is invalid.'
	}
	try {
		$receiptCheckoutIdentity = Get-AgentCanonicalPath $receiptCheckoutRoot
		$receiptCommonIdentity = Get-AgentCanonicalPath $receiptCommonPath
	}
	catch { Throw-CertificationFailure 'certification.receipt-schema' "Candidate receipt checkout identity is unreadable: $($_.Exception.Message)" }
	if (-not $receiptCommonIdentity.Equals($repositoryCommon, [StringComparison]::OrdinalIgnoreCase)) {
		Throw-CertificationFailure 'certification.receipt-repository' 'Candidate receipt belongs to a different repository.'
	}
	$sameCheckout = $receiptCheckoutIdentity.Equals($worktree, [StringComparison]::OrdinalIgnoreCase)
	try { [void](Invoke-AgentGit @('-C', $repository, 'rev-parse', '--verify', "$receiptSourceCommit^{commit}")) }
	catch { Throw-CertificationFailure 'certification.receipt-schema' 'Candidate receipt sourceCommit is not a commit in this repository.' }
	try {
		$receiptJsonOwner = (@(Invoke-AgentGit @('-C', $repository, 'rev-parse', "$receiptSourceCommit`:ThirdParty/tinygltf"))[0]).Trim()
		$expectedJsonOwner = (@(Invoke-AgentGit @('-C', $repository, 'rev-parse', "$ExpectedCommit`:ThirdParty/tinygltf"))[0]).Trim()
		$jsonRepository = Join-Path $repository 'ThirdParty\tinygltf'
		[void](Invoke-AgentGit @('-C', $jsonRepository, 'rev-parse', '--verify', "$expectedJsonOwner^{commit}"))
		$expectedJsonBlob = (@(Invoke-AgentGit @('-C', $jsonRepository, 'rev-parse', "$expectedJsonOwner`:json.hpp"))[0]).Trim()
	}
	catch { Throw-CertificationFailure 'certification.commit-mismatch' 'Unable to bind primary-resolved json.hpp to its expected ThirdParty/tinygltf Git identity.' }
	if ($receiptJsonOwner -cne $expectedJsonOwner) {
		Throw-CertificationFailure 'certification.commit-mismatch' 'Expected commit ThirdParty/tinygltf identity differs from the candidate source commit.'
	}
	$source = Get-Property $receipt 'source' 'receipt'
	if ([string](Get-Property $source 'algorithm' 'receipt.source') -cne 'sorted-path-bytes-sha256-clean-filter-blob/v1') {
		Throw-CertificationFailure 'certification.receipt-schema' 'Candidate receipt source algorithm is unsupported.'
	}
	$before = Get-Manifest (Get-Property $source 'before' 'receipt.source') 'receipt.source.before'
	$after = Get-Manifest (Get-Property $source 'after' 'receipt.source') 'receipt.source.after'
	if ($before.digest -cne $after.digest -or ($before.manifest | ConvertTo-Json -Compress) -cne ($after.manifest | ConvertTo-Json -Compress)) {
		Throw-CertificationFailure 'certification.source-unstable' 'Candidate receipt source before/after snapshots differ.'
	}
	$dependencies = Get-Property $source 'compilerDependencies' 'receipt.source'
	if ($null -ne (Get-Property $dependencies 'blocker' 'receipt.source.compilerDependencies')) {
		Throw-CertificationFailure 'certification.receipt-schema' 'Candidate receipt retained a compiler-dependency blocker.'
	}
	$dependencyInputs = @(Get-Property $dependencies 'repoLocalInputs' 'receipt.source.compilerDependencies')
	$dependencyLogs = @(Get-Property $dependencies 'logs' 'receipt.source.compilerDependencies')
	if ($dependencyLogs.Count -eq 0 -or $dependencyInputs.Count -eq 0) {
		Throw-CertificationFailure 'certification.receipt-schema' 'Candidate receipt compiler dependency evidence is empty.'
	}
	foreach ($log in $dependencyLogs) {
		$logPath = [string](Get-Property $log 'path' 'receipt.source.compilerDependencies.logs[]')
		$logBytes = Get-Property $log 'bytes' 'receipt.source.compilerDependencies.logs[]'
		$logSha256 = [string](Get-Property $log 'sha256' 'receipt.source.compilerDependencies.logs[]')
		if (-not [IO.Path]::IsPathRooted($logPath) -or $logBytes -isnot [ValueType] -or [int64]$logBytes -le 0 -or $logSha256 -cnotmatch '^[0-9a-f]{64}$') {
			Throw-CertificationFailure 'certification.receipt-schema' 'Candidate receipt has invalid compiler dependency log evidence.'
		}
	}
	$manifestPaths = @($after.manifest | ForEach-Object { $_.path })
	foreach ($dependencyPath in $dependencyInputs) {
		if ($manifestPaths -cnotcontains [string]$dependencyPath) {
			Throw-CertificationFailure 'certification.receipt-schema' "Compiler dependency '$dependencyPath' is absent from the source manifest."
		}
	}

	$currentToolPaths = @(Get-CurrentToolPaths $worktree)
	$manifestToolPaths = @($manifestPaths | Where-Object { $_ -cne 'ThirdParty/tinygltf/json.hpp' })
	if (($currentToolPaths -join "`n") -cne ($manifestToolPaths -join "`n")) {
		Throw-CertificationFailure 'certification.source-membership' 'Current AgentTools source membership differs from the candidate manifest.'
	}
	$commitToolPaths = [Collections.Generic.List[string]]::new()
	foreach ($listedPath in @(Invoke-AgentGit @('-C', $worktree, '-c', 'core.quotePath=false', 'ls-tree', '-r', '--name-only', $ExpectedCommit, '--', 'Tools/WorktreeCli', 'Tools/AgentHarness', 'Tools/ToolCommon'))) {
		$commitToolPaths.Add($listedPath.Trim().Replace('\', '/'))
	}
	$commitToolPaths.Sort([StringComparer]::Ordinal)
	if (($commitToolPaths -join "`n") -cne ($manifestToolPaths -join "`n")) {
		Throw-CertificationFailure 'certification.commit-membership' 'Expected commit AgentTools source membership differs from the candidate manifest.'
	}

	$commitEntries = [Collections.Generic.List[object]]::new()
	foreach ($entry in $after.manifest) {
		$sourceRoot = if ($entry.path -ceq 'ThirdParty/tinygltf/json.hpp') { $repository } else { $worktree }
		$fullPath = Join-Path $sourceRoot ($entry.path.Replace('/', '\'))
		$item = Get-Item -LiteralPath $fullPath -Force -ErrorAction SilentlyContinue
		if ($null -eq $item -or $item.PSIsContainer -or ($entry.path -cne 'ThirdParty/tinygltf/json.hpp' -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) -or
			($sameCheckout -and ($item.Length -ne $entry.bytes -or (Get-Sha256 $fullPath) -cne $entry.sha256))) {
			Throw-CertificationFailure 'certification.source-bytes' "Current source bytes differ from the candidate manifest for '$($entry.path)'."
		}
		$currentBlob = (@(Invoke-AgentGit @('-C', $sourceRoot, 'hash-object', "--path=$($entry.path)", '--', $fullPath))[0]).Trim()
		if ($currentBlob -cne $entry.gitBlob) {
			Throw-CertificationFailure 'certification.source-blob' "Current clean-filter blob differs from the candidate manifest for '$($entry.path)'."
		}
		$commitBlob = if ($entry.path -ceq 'ThirdParty/tinygltf/json.hpp') {
			$expectedJsonBlob
		}
		else {
			(@(Invoke-AgentGit @('-C', $worktree, 'rev-parse', "$ExpectedCommit`:$($entry.path)"))[0]).Trim()
		}
		if ($commitBlob -cne $entry.gitBlob) {
			Throw-CertificationFailure 'certification.commit-mismatch' "Expected commit clean-filter blob differs from the candidate manifest for '$($entry.path)'."
		}
		$commitEntries.Add([ordered]@{ path = $entry.path; gitBlob = $commitBlob })
	}

	$executables = Get-Property $receipt 'executables' 'receipt'
	$certifiedExecutables = [ordered]@{}
	foreach ($name in @('WorktreeCli', 'AgentHarness')) {
		$candidate = Get-Property $executables $name 'receipt.executables'
		$path = [string](Get-Property $candidate 'path' "receipt.executables.$name")
		$sha256 = [string](Get-Property $candidate 'sha256' "receipt.executables.$name")
		$bytes = Get-Property $candidate 'bytes' "receipt.executables.$name"
		$item = Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
		if ($sha256 -cnotmatch '^[0-9a-f]{64}$' -or $bytes -isnot [ValueType] -or [int64]$bytes -le 0 -or
			$null -eq $item -or $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
			$item.Length -ne [int64]$bytes -or (Get-Sha256 $path) -cne $sha256) {
			Throw-CertificationFailure 'certification.candidate-identity' "Candidate executable '$name' is missing or differs from its receipt identity."
		}
		$certifiedExecutables[$name] = [ordered]@{ path = $item.FullName; bytes = $item.Length; sha256 = $sha256 }
	}
	$toolchain = Get-Property $receipt 'toolchain' 'receipt'
	$msBuild = Get-Property $toolchain 'msBuild' 'receipt.toolchain'
	if (-not [IO.Path]::IsPathRooted([string](Get-Property $msBuild 'path' 'receipt.toolchain.msBuild')) -or
		(Get-Property $msBuild 'bytes' 'receipt.toolchain.msBuild') -isnot [ValueType] -or [int64]$msBuild.bytes -le 0 -or
		[string](Get-Property $msBuild 'sha256' 'receipt.toolchain.msBuild') -cnotmatch '^[0-9a-f]{64}$' -or
		[string](Get-Property $toolchain 'configuration' 'receipt.toolchain') -cne 'Release' -or
		[string](Get-Property $toolchain 'platform' 'receipt.toolchain') -cne 'x64') {
		Throw-CertificationFailure 'certification.receipt-schema' 'Candidate receipt toolchain identity is invalid.'
	}
	[void](Get-Property $msBuild 'fileVersion' 'receipt.toolchain.msBuild')
	[void](Get-Property $msBuild 'productVersion' 'receipt.toolchain.msBuild')
	$canonical = Get-Property $receipt 'canonical' 'receipt'
	$canonicalBefore = Get-Property $canonical 'before' 'receipt.canonical'
	$canonicalAfter = Get-Property $canonical 'after' 'receipt.canonical'
	foreach ($name in @('worktreeCli', 'agentHarness')) {
		Assert-ReceiptFileIdentity (Get-Property $canonicalBefore $name 'receipt.canonical.before') "receipt.canonical.before.$name" $true
		Assert-ReceiptFileIdentity (Get-Property $canonicalAfter $name 'receipt.canonical.after') "receipt.canonical.after.$name" $true
	}
	if (($canonicalBefore | ConvertTo-Json -Compress) -cne ($canonicalAfter | ConvertTo-Json -Compress) -or (Get-Property $canonical 'unchanged' 'receipt.canonical') -ne $true) {
		Throw-CertificationFailure 'certification.receipt-schema' 'Candidate receipt canonical before/after identities are not unchanged.'
	}
	$capability = Get-Property $receipt 'capability' 'receipt'
	$capabilityScript = Get-Property $capability 'script' 'receipt.capability'
	if ([string](Get-Property $capability 'status' 'receipt.capability') -cne 'pass' -or
		-not [IO.Path]::IsPathRooted([string](Get-Property $capabilityScript 'path' 'receipt.capability.script')) -or
		(Get-Property $capabilityScript 'bytes' 'receipt.capability.script') -isnot [ValueType] -or [int64]$capabilityScript.bytes -le 0 -or
		[string](Get-Property $capabilityScript 'sha256' 'receipt.capability.script') -cnotmatch '^[0-9a-f]{64}$') {
		Throw-CertificationFailure 'certification.receipt-schema' 'Candidate receipt does not attest unchanged canonical outputs and a passing capability check.'
	}
	$treeHashes = @(Invoke-AgentGit @('-C', $worktree, 'rev-parse', "$ExpectedCommit`:Tools/WorktreeCli", "$ExpectedCommit`:Tools/AgentHarness", "$ExpectedCommit`:Tools/ToolCommon") | ForEach-Object { $_.Trim() })
	if ($treeHashes.Count -ne 3 -or @($treeHashes | Where-Object { $_ -cnotmatch '^[0-9a-f]{40}$' }).Count -ne 0) {
		throw 'Unable to resolve expected commit AgentTools tree identities.'
	}

	$result.receipt = [ordered]@{ path = (Get-Item -LiteralPath $CandidateReceiptPath -Force).FullName; sha256 = $actualReceiptSha256; schemaVersion = $receipt.schemaVersion }
	$result.source = [ordered]@{
		digest = $after.digest
		manifestCount = $after.manifest.Count
		commitEntries = $commitEntries.ToArray()
		commitIdentities = [ordered]@{ worktreeCli = $treeHashes[0]; agentHarness = $treeHashes[1]; toolCommon = $treeHashes[2] }
		primaryResolvedJson = [ordered]@{ path = 'ThirdParty/tinygltf/json.hpp'; ownerGitObject = $expectedJsonOwner; gitBlob = ($commitEntries | Where-Object { $_.path -ceq 'ThirdParty/tinygltf/json.hpp' }).gitBlob }
	}
	$result.executables = $certifiedExecutables
	$result.status = 'pass'
	$result.code = 'ok'
	$result.message = 'AgentTools candidate is certified against the expected commit and current source identities.'
	$exitCode = 0
}
catch {
	if ($_.Exception.Data.Contains('CertificationFailure')) {
		$result.status = 'blocked'
		$result.code = [string]$_.Exception.Data['Code']
		$result.message = $_.Exception.Message
		$exitCode = 2
	}
	else {
		$result.status = 'error'
		$result.code = 'certification.failed'
		$result.message = $_.Exception.Message
		$exitCode = 1
	}
}

[Console]::Out.Write(($result | ConvertTo-Json -Depth 100 -Compress))
exit $exitCode

Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'AgentScriptCommon.psm1')
Import-Module (Join-Path $PSScriptRoot 'WorktreeCliSessionExclusion.psm1') -DisableNameChecking

$script:ReceiptSchemaVersion = 'broken-engine-agent-worktree-receipt/v1'
$script:ReceiptFileName = 'AgentWorktreeSessionReceipt.json'
$script:ReceiptIntegrityFileName = 'AgentWorktreeSessionReceipt.sha256'

function Test-AgentWorktreeStrictUtc([object] $Value) {
	if ($Value -isnot [string]) { return $false }
	$parsed = [DateTime]::MinValue
	if (-not [DateTime]::TryParseExact($Value, 'O', [Globalization.CultureInfo]::InvariantCulture,
		[Globalization.DateTimeStyles]::RoundtripKind, [ref]$parsed)) { return $false }
	return $parsed.Kind -eq [DateTimeKind]::Utc -and $parsed.ToString('O', [Globalization.CultureInfo]::InvariantCulture) -ceq $Value
}

function Test-AgentWorktreeGuid([object] $Value) {
	if ($Value -isnot [string]) { return $false }
	$guid = [guid]::Empty
	return [guid]::TryParseExact($Value, 'D', [ref]$guid) -and $guid.ToString() -ceq $Value
}

function Get-AgentWorktreeGitValue([string] $Worktree, [string[]] $Arguments, [string] $Description) {
	$value = @(Invoke-AgentGit (@('-C', $Worktree) + $Arguments))
	if ($value.Count -ne 1 -or [string]::IsNullOrWhiteSpace($value[0])) {
		throw "Git returned no unique $Description for '$Worktree'."
	}
	return $value[0].Trim()
}

function Get-AgentWorktreePrivateGitDirectory([string] $Worktree) {
	$directory = Get-AgentWorktreeGitValue $Worktree @('rev-parse', '--path-format=absolute', '--git-dir') 'private Git directory'
	$directory = Get-AgentCanonicalPath $directory
	$item = Get-Item -LiteralPath $directory -Force -ErrorAction Stop
	if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
		throw "Linked worktree private Git directory is not an ordinary directory: '$directory'."
	}
	return $directory
}

function Get-AgentWorktreeReceiptPath([string] $Worktree) {
	return Join-Path (Get-AgentWorktreePrivateGitDirectory $Worktree) $script:ReceiptFileName
}

function Get-AgentWorktreeReceiptIntegrityPath([string] $Worktree) {
	return Join-Path (Get-AgentWorktreePrivateGitDirectory $Worktree) $script:ReceiptIntegrityFileName
}

function Get-AgentWorktreeReceiptSha256([byte[]] $Bytes) {
	return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}

function Open-AgentWorktreeReceiptReadLease([string] $Worktree) {
	$path = Get-AgentWorktreeReceiptPath $Worktree
	$integrityPath = Get-AgentWorktreeReceiptIntegrityPath $Worktree
	foreach ($candidate in @($path, $integrityPath)) {
		$item = Get-Item -LiteralPath $candidate -Force -ErrorAction Stop
		if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
			throw "Worktree receipt authority artifact is not an ordinary file: '$candidate'."
		}
	}
	$integrityStream = $null
	try {
		$integrityStream = [IO.File]::Open($integrityPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
		$receiptStream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
		return [pscustomobject]@{ Path = $path; IntegrityPath = $integrityPath; ReceiptStream = $receiptStream; IntegrityStream = $integrityStream }
	}
	catch {
		if ($null -ne $integrityStream) { $integrityStream.Dispose() }
		throw
	}
}

function Read-AgentWorktreeReceiptLeaseBytes([IO.FileStream] $Stream, [string] $Path) {
	if ($Stream.Length -gt [int]::MaxValue) { throw "Worktree receipt authority artifact is too large: '$Path'." }
	$Stream.Position = 0
	$bytes = [byte[]]::new([int]$Stream.Length)
	$offset = 0
	while ($offset -lt $bytes.Length) {
		$count = $Stream.Read($bytes, $offset, $bytes.Length - $offset)
		if ($count -eq 0) { throw "Worktree receipt authority artifact changed while read: '$Path'." }
		$offset += $count
	}
	return $bytes
}

function Write-AgentWorktreeOrdinaryFileAtomic([string] $Path, [byte[]] $Bytes) {
	$temp = Join-Path (Split-Path -Parent $Path) ('.' + [guid]::NewGuid().ToString() + '.tmp')
	try {
		$stream = [IO.File]::Open($temp, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
		try { $stream.Write($Bytes, 0, $Bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
		[IO.File]::Move($temp, $Path)
	}
	finally {
		if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Force }
	}
}

function New-AgentWorktreeSessionReceipt {
	[CmdletBinding()] param(
		[Parameter(Mandatory)][ValidateSet('claude', 'codex')][string] $Client,
		[Parameter(Mandatory)][string] $PrimaryCheckout,
		[Parameter(Mandatory)][string] $GitCommonDirectory,
		[Parameter(Mandatory)][string] $Worktree,
		[Parameter(Mandatory)][string] $WorktreeId,
		[Parameter(Mandatory)][string] $Branch,
		[Parameter(Mandatory)][string] $TargetBranch,
		[Parameter(Mandatory)][string] $Baseline,
		[Parameter(Mandatory)][string] $SessionOwner
	)
	if (-not (Test-AgentWorktreeGuid $WorktreeId) -or -not (Test-AgentWorktreeGuid $SessionOwner)) { throw 'Receipt worktree and session identities must be canonical lowercase GUIDs.' }
	if ($Branch -cne "$Client/$WorktreeId") { throw "Receipt branch '$Branch' does not match client and worktree identity." }
	if ($Baseline -cnotmatch '^[0-9a-f]{40}$') { throw "Receipt baseline is not a full lowercase commit hash: '$Baseline'." }
	if ([string]::IsNullOrWhiteSpace($TargetBranch) -or $TargetBranch.Contains("`0")) { throw 'Receipt target branch is invalid.' }
	return [ordered]@{
		schemaVersion = $script:ReceiptSchemaVersion
		client = $Client
		primaryCheckout = Get-AgentCanonicalPath $PrimaryCheckout
		gitCommonDirectory = Get-AgentCanonicalPath $GitCommonDirectory
		worktree = Get-AgentCanonicalPath $Worktree
		worktreeId = $WorktreeId
		branch = $Branch
		targetBranch = $TargetBranch
		baseline = $Baseline
		sessionOwner = $SessionOwner
		createdUtc = [DateTime]::UtcNow.ToString('O', [Globalization.CultureInfo]::InvariantCulture)
	}
}

function Write-AgentWorktreeSessionReceipt([string] $Worktree, [System.Collections.IDictionary] $Receipt) {
	$path = Get-AgentWorktreeReceiptPath $Worktree
	$integrityPath = Get-AgentWorktreeReceiptIntegrityPath $Worktree
	if ((Test-Path -LiteralPath $path) -or (Test-Path -LiteralPath $integrityPath)) { throw "Worktree receipt or integrity reference already exists: '$path'." }
	$bytes = [Text.UTF8Encoding]::new($false).GetBytes(($Receipt | ConvertTo-Json -Depth 8 -Compress))
	try {
		Write-AgentWorktreeOrdinaryFileAtomic $path $bytes
		Write-AgentWorktreeOrdinaryFileAtomic $integrityPath ([Text.UTF8Encoding]::new($false).GetBytes((Get-AgentWorktreeReceiptSha256 $bytes)))
	}
	catch {
		Remove-Item -LiteralPath @($path, $integrityPath) -Force -ErrorAction SilentlyContinue
		throw
	}
	return $path
}

function Read-AgentWorktreeSessionReceipt {
	[CmdletBinding()] param(
		[Parameter(Mandatory)][string] $Worktree,
		[object] $ReadLease
	)
	$path = Get-AgentWorktreeReceiptPath $Worktree
	$integrityPath = Get-AgentWorktreeReceiptIntegrityPath $Worktree
	if ($null -ne $ReadLease) {
		if ($ReadLease.Path -cne $path -or $ReadLease.IntegrityPath -cne $integrityPath) { throw 'Worktree receipt read lease does not match requested worktree.' }
		$integrityBytes = Read-AgentWorktreeReceiptLeaseBytes $ReadLease.IntegrityStream $integrityPath
		$bytes = Read-AgentWorktreeReceiptLeaseBytes $ReadLease.ReceiptStream $path
	}
	else {
		$item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
		if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Worktree receipt is not an ordinary file: '$path'." }
		$integrityItem = Get-Item -LiteralPath $integrityPath -Force -ErrorAction Stop
		if ($integrityItem.PSIsContainer -or ($integrityItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Worktree receipt integrity reference is not an ordinary file: '$integrityPath'." }
		$integrityBytes = [IO.File]::ReadAllBytes($integrityPath)
		$bytes = [IO.File]::ReadAllBytes($path)
	}
	try { $integrity = [Text.UTF8Encoding]::new($false, $true).GetString($integrityBytes) }
	catch { throw "Worktree receipt integrity reference is unreadable or malformed: '$integrityPath': $($_.Exception.Message)" }
	if ($integrity -cnotmatch '^[0-9a-f]{64}$') { throw "Worktree receipt integrity reference failed strict validation: '$integrityPath'." }
	if ((Get-AgentWorktreeReceiptSha256 $bytes) -cne $integrity) { throw "Worktree receipt integrity reference does not match receipt bytes: '$integrityPath'." }
	try {
		$convertArguments = if ((Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')) { @{ DateKind = 'String' } } else { @{} }
		$receipt = ([Text.UTF8Encoding]::new($false, $true).GetString($bytes) | ConvertFrom-Json @convertArguments -ErrorAction Stop)
	}
	catch { throw "Worktree receipt is unreadable or malformed: '$path': $($_.Exception.Message)" }
	$names = if ($receipt -is [pscustomobject]) { @($receipt.PSObject.Properties.Name) } else { @() }
	$required = @('schemaVersion', 'client', 'primaryCheckout', 'gitCommonDirectory', 'worktree', 'worktreeId', 'branch', 'targetBranch', 'baseline', 'sessionOwner', 'createdUtc')
	if ($names.Count -ne $required.Count -or @($required | Where-Object { $_ -notin $names }).Count -ne 0 -or
		@($required | Where-Object { $receipt.$_ -isnot [string] }).Count -ne 0 -or
		$receipt.schemaVersion -cne $script:ReceiptSchemaVersion -or
		$receipt.client -cne 'claude' -and $receipt.client -cne 'codex' -or
		-not (Test-AgentWorktreeGuid $receipt.worktreeId) -or -not (Test-AgentWorktreeGuid $receipt.sessionOwner) -or
		$receipt.branch -cne "$($receipt.client)/$($receipt.worktreeId)" -or
		$receipt.baseline -isnot [string] -or $receipt.baseline -cnotmatch '^[0-9a-f]{40}$' -or
		-not (Test-AgentWorktreeStrictUtc $receipt.createdUtc)) {
		throw "Worktree receipt failed strict schema validation: '$path'."
	}
	foreach ($name in @('primaryCheckout', 'gitCommonDirectory', 'worktree', 'targetBranch')) {
		if ($receipt.$name -isnot [string] -or [string]::IsNullOrWhiteSpace($receipt.$name) -or $receipt.$name.Contains("`0")) {
			throw "Worktree receipt has an invalid '$name' value: '$path'."
		}
	}
	foreach ($name in @('primaryCheckout', 'gitCommonDirectory', 'worktree')) {
		if ((Get-AgentCanonicalPath $receipt.$name) -cne $receipt.$name) {
			throw "Worktree receipt '$name' is not canonical: '$path'."
		}
	}
	return [pscustomobject]@{ Path = $path; Bytes = $bytes; IntegrityPath = $integrityPath; IntegrityBytes = $integrityBytes; Value = $receipt }
}

function Get-AgentWorktreeRecords([string] $RepositoryRoot) {
	$records = [Collections.Generic.List[object]]::new()
	$current = $null
	$output = Get-AgentWorktreeGitValue $RepositoryRoot @('worktree', 'list', '--porcelain', '-z') 'worktree registration list'
	foreach ($field in $output.Split([char]0, [StringSplitOptions]::RemoveEmptyEntries)) {
		if ($field.StartsWith('worktree ', [StringComparison]::Ordinal)) {
			if ($null -ne $current) { $records.Add([pscustomobject] $current) }
			$current = [ordered]@{ Path = Get-AgentCanonicalPath $field.Substring(9); Head = $null; Branch = $null; Prunable = $false; Bare = $false }
		}
		elseif ($null -ne $current -and $field.StartsWith('HEAD ', [StringComparison]::Ordinal)) { $current.Head = $field.Substring(5) }
		elseif ($null -ne $current -and $field.StartsWith('branch refs/heads/', [StringComparison]::Ordinal)) { $current.Branch = $field.Substring(18) }
		elseif ($null -ne $current -and $field.StartsWith('prunable', [StringComparison]::Ordinal)) { $current.Prunable = $true }
		elseif ($null -ne $current -and $field -ceq 'bare') { $current.Bare = $true }
	}
	if ($null -ne $current) { $records.Add([pscustomobject] $current) }
	return $records.ToArray()
}

function Test-AgentWorktreeNoGitOperation([string] $Worktree) {
	foreach ($marker in @('MERGE_HEAD', 'CHERRY_PICK_HEAD', 'REVERT_HEAD', 'BISECT_LOG', 'rebase-merge', 'rebase-apply', 'sequencer')) {
		$path = Get-AgentWorktreeGitValue $Worktree @('rev-parse', '--path-format=absolute', '--git-path', $marker) "Git operation marker '$marker'"
		if (Test-Path -LiteralPath $path) { throw "Git operation marker '$marker' is active in '$Worktree'." }
	}
}

function Test-AgentWorktreeAncestor([string] $RepositoryRoot, [string] $Ancestor, [string] $Descendant, [string] $Description) {
	& git -C $RepositoryRoot merge-base --is-ancestor $Ancestor $Descendant
	if ($LASTEXITCODE -ne 0) { throw "Receipt baseline '$Ancestor' is not an ancestor of $Description '$Descendant'." }
}

function Get-AgentWorktreePrimaryIdentity([string] $RepositoryRoot) {
	$root = Get-AgentCanonicalPath $RepositoryRoot
	$top = Get-AgentWorktreeGitValue $root @('rev-parse', '--show-toplevel') 'repository top-level'
	$top = Get-AgentCanonicalPath $top
	if (-not $root.Equals($top, [StringComparison]::OrdinalIgnoreCase)) { throw "RepositoryRoot is not repository root: '$root'." }
	$git = Get-Item -LiteralPath (Join-Path $root '.git') -Force -ErrorAction Stop
	if (-not $git.PSIsContainer -or ($git.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "RepositoryRoot is not an ordinary primary checkout: '$root'." }
	$common = Get-AgentWorktreeGitValue $root @('rev-parse', '--path-format=absolute', '--git-common-dir') 'Git common directory'
	$common = Get-AgentCanonicalPath $common
	if (-not $common.Equals((Get-AgentCanonicalPath $git.FullName), [StringComparison]::OrdinalIgnoreCase)) { throw "RepositoryRoot does not own its Git common directory: '$root'." }
	$branch = Get-AgentWorktreeGitValue $root @('branch', '--show-current') 'primary branch'
	$head = Get-AgentWorktreeGitValue $root @('rev-parse', 'HEAD') 'primary HEAD'
	if ($head -cnotmatch '^[0-9a-f]{40}$') { throw "Primary HEAD is malformed: '$head'." }
	Test-AgentWorktreeNoGitOperation $root
	return [pscustomobject]@{ Root = $root; CommonDirectory = $common; Branch = $branch; Head = $head }
}

function Get-AgentWorktreeReattachProof {
	[CmdletBinding()] param(
		[Parameter(Mandatory)][ValidateSet('claude', 'codex')][string] $Client,
		[Parameter(Mandatory)][string] $RepositoryRoot,
		[Parameter(Mandatory)][string] $Worktree,
		[byte[]] $ExpectedReceiptBytes,
		[byte[]] $ExpectedReceiptIntegrityBytes,
		[object] $ReadLease
	)
	if ($Client -cne 'claude' -and $Client -cne 'codex') { throw "Client must be lowercase 'claude' or 'codex'." }
	$primary = Get-AgentWorktreePrimaryIdentity $RepositoryRoot
	$worktree = Get-AgentCanonicalPath $Worktree
	$receipt = Read-AgentWorktreeSessionReceipt -Worktree $worktree -ReadLease $ReadLease
	if ($PSBoundParameters.ContainsKey('ExpectedReceiptBytes') -and -not [System.Collections.StructuralComparisons]::StructuralEqualityComparer.Equals($receipt.Bytes, $ExpectedReceiptBytes)) {
		throw "Worktree receipt bytes changed during reattach proof: '$($receipt.Path)'."
	}
	if ($PSBoundParameters.ContainsKey('ExpectedReceiptIntegrityBytes') -and -not [System.Collections.StructuralComparisons]::StructuralEqualityComparer.Equals($receipt.IntegrityBytes, $ExpectedReceiptIntegrityBytes)) {
		throw "Worktree receipt integrity reference changed during reattach proof: '$($receipt.IntegrityPath)'."
	}
	$value = $receipt.Value
	if ($value.client -cne $Client -or -not $value.primaryCheckout.Equals($primary.Root, [StringComparison]::OrdinalIgnoreCase) -or
		-not $value.gitCommonDirectory.Equals($primary.CommonDirectory, [StringComparison]::OrdinalIgnoreCase) -or
		-not $value.worktree.Equals($worktree, [StringComparison]::OrdinalIgnoreCase)) {
		throw 'Worktree receipt client or repository identity does not match the requested reattach.'
	}
	$records = @(Get-AgentWorktreeRecords $primary.Root | Where-Object { -not $_.Bare -and $_.Path.Equals($worktree, [StringComparison]::OrdinalIgnoreCase) })
	if ($records.Count -ne 1 -or $records[0].Prunable -or $records[0].Branch -cne $value.branch) {
		throw 'Recorded worktree is not uniquely registered, is prunable, or has a different branch.'
	}
	$worktreeTop = Get-AgentCanonicalPath (Get-AgentWorktreeGitValue $worktree @('rev-parse', '--show-toplevel') 'worktree top-level')
	if (-not $worktreeTop.Equals($worktree, [StringComparison]::OrdinalIgnoreCase)) { throw "Recorded worktree is not its Git top-level: '$worktree'." }
	$worktreeCommon = Get-AgentCanonicalPath (Get-AgentWorktreeGitValue $worktree @('rev-parse', '--path-format=absolute', '--git-common-dir') 'worktree Git common directory')
	if (-not $worktreeCommon.Equals($primary.CommonDirectory, [StringComparison]::OrdinalIgnoreCase)) { throw 'Recorded worktree does not share the recorded Git common directory.' }
	$worktreeHead = Get-AgentWorktreeGitValue $worktree @('rev-parse', 'HEAD') 'worktree HEAD'
	if ($worktreeHead -cnotmatch '^[0-9a-f]{40}$') { throw "Recorded worktree HEAD is malformed: '$worktreeHead'." }
	if ($primary.Branch -cne $value.targetBranch) { throw "Recorded primary checkout is not on receipt target branch '$($value.targetBranch)'." }
	Test-AgentWorktreeNoGitOperation $worktree
	Test-AgentWorktreeAncestor $primary.Root $value.baseline $primary.Head 'primary HEAD'
	Test-AgentWorktreeAncestor $primary.Root $value.baseline $worktreeHead 'worktree HEAD'
	return [pscustomobject]@{ Receipt = $receipt; Primary = $primary; Worktree = $worktree; WorktreeHead = $worktreeHead }
}

Export-ModuleMember -Function Get-AgentWorktreePrimaryIdentity, Get-AgentWorktreePrivateGitDirectory, Get-AgentWorktreeReceiptPath, Get-AgentWorktreeReceiptIntegrityPath, Open-AgentWorktreeReceiptReadLease, New-AgentWorktreeSessionReceipt, Write-AgentWorktreeSessionReceipt, Read-AgentWorktreeSessionReceipt, Get-AgentWorktreeReattachProof

# Assembles the /codex-review dispatch prompt file and returns only a small receipt, so the diff
# evidence never passes through the calling manager's context. The fixed wording comes from
# ../references/prompt-template.md; the changed-file set comes from Get-SessionChangeInventory.ps1;
# the judgment text comes from the manager-authored -ScopeFile and is copied verbatim. The script
# writes a file, so it is manager-side only: the reviewer sandbox only reads the prompt.
# Diagnostics go to stderr; stdout carries only the receipt.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $RepositoryRoot,
	[Parameter(Mandatory)][string] $Baseline,
	[Parameter(Mandatory)][string] $AssignedSkill,
	[Parameter(Mandatory)][string] $ScopeFile,
	[Parameter(Mandatory)][string] $PromptPath,
	[ValidateRange(1, 3)][int] $RiskTier = 0,
	[string[]] $UntrackedPath,
	[string] $Head
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\AgentScriptCommon.psm1') -Force

$script:MaximumPromptBytes = 4 * 1024 * 1024
$script:MaximumMessageLength = 256
$script:MaximumContributors = 5
$script:BinaryProbeBytes = 8192
$script:CopyBufferBytes = 65536
# One ranking has to mix a tracked diff measured in changed lines with an untracked file measured in
# bytes, so untracked bytes are normalized by this rough bytes-per-line figure.
$script:BytesPerLine = 50

$script:Utf8 = [Text.UTF8Encoding]::new($false)
$script:Inventory = Join-Path $PSScriptRoot '..\..\..\scripts\Get-SessionChangeInventory.ps1'
$script:Template = Join-Path $PSScriptRoot '..\references\prompt-template.md'
$script:Root = $null
$script:PromptFile = $null
$script:PromptStream = $null
$script:PromptCreated = $false
$script:PromptBytes = 0
$script:SectionCount = 0
$script:DiffRange = @()
$script:DiffPath = @()
$script:Untracked = @()

$result = [ordered]@{
	schemaVersion = 'broken-engine-codex-review-prompt/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Codex review prompt assembly did not run.'
	promptPath = $null
	promptBytes = 0
	fileCount = 0
	binaryExcluded = 0
	sectionsWritten = 0
}

function Write-PromptStderr([string] $Text) {
	$stream = [Console]::OpenStandardError()
	$bytes = $script:Utf8.GetBytes($Text)
	$stream.Write($bytes, 0, $bytes.Length)
	$stream.Flush()
}

function Complete-CodexReviewPrompt([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	if ($null -ne $script:PromptStream) { $script:PromptStream.Dispose(); $script:PromptStream = $null }
	# A non-pass run leaves no partial prompt behind, and an existing prompt path is refused before
	# anything is created, so the caller's file is never the one removed here.
	if ($ExitCode -ne 0 -and $script:PromptCreated) {
		Remove-Item -LiteralPath $script:PromptFile -Force -ErrorAction SilentlyContinue
		$script:PromptCreated = $false
	}
	$result.status = $Status
	$result.code = $Code
	$result.message = if ($Message.Length -gt $script:MaximumMessageLength) { $Message.Substring(0, $script:MaximumMessageLength) } else { $Message }
	$result.promptPath = if ($script:PromptCreated) { $script:PromptFile } else { $null }
	$result.promptBytes = if ($script:PromptCreated) { $script:PromptBytes } else { 0 }
	$result.sectionsWritten = if ($script:PromptCreated) { $script:SectionCount } else { 0 }
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 32 -Compress))
	exit $ExitCode
}

function Invoke-PromptGit([string[]] $Arguments, [bool] $AllowFailure = $false) {
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = 'git'
	$start.WorkingDirectory = $script:Root
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	$start.StandardOutputEncoding = $script:Utf8
	$start.StandardErrorEncoding = $script:Utf8
	$start.Environment['GIT_OPTIONAL_LOCKS'] = '0'
	foreach ($argument in @('-C', $script:Root, '--no-pager') + $Arguments) { [void] $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw "Could not start git for: $($Arguments -join ' ')" }
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$run = [pscustomobject] @{
		ExitCode = $process.ExitCode
		Stdout = $stdoutTask.GetAwaiter().GetResult()
		Stderr = $stderrTask.GetAwaiter().GetResult()
	}
	$process.Dispose()
	if (-not $AllowFailure -and $run.ExitCode -ne 0) { throw "git $($Arguments -join ' ') failed with exit $($run.ExitCode): $($run.Stderr.Trim())" }
	return $run
}

function Get-PromptRelativePath([string] $Path) {
	# The same normalization the inventory applies to its -IncludeUntracked values, so a listed path
	# and an inventory entry compare as the same repository-relative path.
	$normalized = $Path.Replace('\', '/').Trim()
	if ([IO.Path]::IsPathRooted($normalized)) {
		$rootPosix = $script:Root.Replace('\', '/').TrimEnd('/') + '/'
		if ($normalized.StartsWith($rootPosix, [StringComparison]::OrdinalIgnoreCase)) { $normalized = $normalized.Substring($rootPosix.Length) }
	}
	if ($normalized.StartsWith('./')) { $normalized = $normalized.Substring(2) }
	return $normalized
}

function Get-PromptFullPath([string] $RelativePath) {
	return Join-Path $script:Root ($RelativePath -replace '/', [IO.Path]::DirectorySeparatorChar)
}

function Get-PromptFragment([string] $Text, [string] $Name) {
	$pattern = "(?s)<!-- fragment: $([Regex]::Escape($Name)) -->\r?\n(.*?)\r?\n<!-- end-fragment: $([Regex]::Escape($Name)) -->"
	$match = [Regex]::Match($Text, $pattern)
	if (-not $match.Success) {
		Complete-CodexReviewPrompt 1 'error' 'prompt.template-invalid' "The prompt template has no '$Name' fragment: '$($script:Template)'."
	}
	return $match.Groups[1].Value.Replace("`r`n", "`n")
}

function Test-PromptBinaryContent([string] $RelativePath) {
	# Content decides, not the extension: a binary payload named .md or .ps1 must never be inlined.
	# Git's own heuristic is a NUL byte in the first probe window.
	$stream = [IO.File]::Open((Get-PromptFullPath $RelativePath), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
	try {
		$buffer = [byte[]]::new($script:BinaryProbeBytes)
		$read = $stream.Read($buffer, 0, $buffer.Length)
		for ($index = 0; $index -lt $read; $index++) {
			if ($buffer[$index] -eq 0) { return $true }
		}
		return $false
	}
	finally { $stream.Dispose() }
}

function Get-PromptContributor() {
	$contributors = [Collections.Generic.List[object]]::new()
	if ($script:DiffPath.Count -gt 0) {
		$arguments = @('diff', '--numstat', '-M', '--no-color', '--no-ext-diff') + $script:DiffRange + @('--') + $script:DiffPath
		$run = Invoke-PromptGit $arguments $true
		foreach ($line in ($run.Stdout -split "`n")) {
			if ([string]::IsNullOrWhiteSpace($line)) { continue }
			$fields = $line.TrimEnd("`r") -split "`t"
			if ($fields.Count -lt 3) { continue }
			if ($fields[0] -ceq '-') {
				$contributors.Add([pscustomobject] @{ Weight = 0; Text = "$($fields[2]) (binary)" })
				continue
			}
			$changed = [int] $fields[0] + [int] $fields[1]
			$contributors.Add([pscustomobject] @{ Weight = $changed; Text = "$($fields[2]) ($changed changed line(s))" })
		}
	}
	foreach ($entry in $script:Untracked) {
		if ($entry.Binary) { continue }
		$length = (Get-Item -LiteralPath (Get-PromptFullPath $entry.Path) -Force).Length
		$contributors.Add([pscustomobject] @{ Weight = [int] ($length / $script:BytesPerLine); Text = "$($entry.Path) ($length untracked byte(s))" })
	}
	$ranked = @($contributors | Sort-Object -Property @{ Expression = 'Weight'; Descending = $true }, @{ Expression = 'Text' } | Select-Object -First $script:MaximumContributors)
	if ($ranked.Count -eq 0) { return 'none' }
	return (($ranked | ForEach-Object { $_.Text }) -join '; ')
}

function Complete-PromptOverBudget() {
	$largest = Get-PromptContributor
	Complete-CodexReviewPrompt 2 'blocked' 'prompt.diff-too-large' "The assembled prompt passed the $($script:MaximumPromptBytes)-byte budget; evidence is never truncated, so split the review. Largest contributing paths: $largest"
}

function Add-PromptText([string] $Text) {
	$bytes = $script:Utf8.GetBytes($Text)
	$script:PromptStream.Write($bytes, 0, $bytes.Length)
	$script:PromptBytes += $bytes.Length
	if ($script:PromptBytes -gt $script:MaximumPromptBytes) { Complete-PromptOverBudget }
}

function Add-PromptStream([IO.Stream] $Source, [Diagnostics.Process] $Process) {
	$buffer = [byte[]]::new($script:CopyBufferBytes)
	while ($true) {
		$read = $Source.Read($buffer, 0, $buffer.Length)
		if ($read -le 0) { break }
		$script:PromptStream.Write($buffer, 0, $read)
		$script:PromptBytes += $read
		if ($script:PromptBytes -gt $script:MaximumPromptBytes) {
			# Stop draining the child before reporting, so an unbounded diff cannot keep writing.
			if ($null -ne $Process -and -not $Process.HasExited) { $Process.Kill() }
			Complete-PromptOverBudget
		}
	}
}

function Write-PromptSection([string] $Heading, [string] $Body) {
	if ($script:SectionCount -gt 0) { Add-PromptText "`n---`n`n" }
	Add-PromptText "# $Heading`n`n"
	if (-not [string]::IsNullOrEmpty($Body)) { Add-PromptText $Body }
	$script:SectionCount++
}

function Write-GuardrailBlock([string] $Guardrails) {
	Add-PromptText "## Guardrails`n`n"
	Add-PromptText $Guardrails
	Add-PromptText "`n"
}

function Get-ChangedFileSet([string[]] $Listed) {
	$arguments = @('-NoProfile', '-File', $script:Inventory, '-RepositoryRoot', $script:Root, '-Baseline', $Baseline)
	if (-not [string]::IsNullOrWhiteSpace($Head)) { $arguments += @('-Head', $Head) }
	if ($Listed.Count -gt 0) { $arguments += @('-IncludeUntracked', ($Listed -join ',')) }
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = (Get-Process -Id $PID).Path
	$start.WorkingDirectory = $script:Root
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	$start.StandardOutputEncoding = $script:Utf8
	$start.StandardErrorEncoding = $script:Utf8
	foreach ($argument in $arguments) { [void] $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw 'Could not start pwsh for the session change inventory.' }
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$exitCode = $process.ExitCode
	$stdout = $stdoutTask.GetAwaiter().GetResult()
	$stderr = $stderrTask.GetAwaiter().GetResult()
	$process.Dispose()
	$inventory = $null
	if (-not [string]::IsNullOrWhiteSpace($stdout)) { try { $inventory = $stdout | ConvertFrom-Json -Depth 32 } catch { } }
	if ($null -eq $inventory) {
		if (-not [string]::IsNullOrWhiteSpace($stderr)) { Write-PromptStderr $stderr }
		Complete-CodexReviewPrompt 1 'error' 'prompt.inventory-failed' "The session change inventory returned no usable result (exit $exitCode)."
	}
	if ($exitCode -ne 0) {
		Complete-CodexReviewPrompt 2 'blocked' 'prompt.inventory-blocked' "The session change inventory blocked with $($inventory.code): $($inventory.message)"
	}
	# Evidence is complete or the run blocks: a capped entry list, or an untracked path the manager
	# did not name, would let the review report a clean result over bytes it never saw.
	if ($inventory.truncated) {
		Complete-CodexReviewPrompt 2 'blocked' 'prompt.inventory-truncated' 'The session change inventory truncated its entry list; narrow the baseline or the reviewed scope so every changed path is listed.'
	}
	if ($inventory.counts.unlistedUntracked -ne 0) {
		Complete-CodexReviewPrompt 2 'blocked' 'prompt.inventory-truncated' "The worktree holds $($inventory.counts.unlistedUntracked) untracked path(s) that -UntrackedPath does not name; name them or remove them so the evidence is complete."
	}
	return $inventory
}

function Write-DiffEvidence() {
	if ($script:DiffPath.Count -gt 0) {
		Add-PromptText "## Diff`n`n"
		$arguments = @('diff', '-M', '--no-color', '--no-ext-diff') + $script:DiffRange + @('--') + $script:DiffPath
		$start = [Diagnostics.ProcessStartInfo]::new()
		$start.FileName = 'git'
		$start.WorkingDirectory = $script:Root
		$start.UseShellExecute = $false
		$start.CreateNoWindow = $true
		$start.RedirectStandardOutput = $true
		$start.RedirectStandardError = $true
		$start.StandardErrorEncoding = $script:Utf8
		$start.Environment['GIT_OPTIONAL_LOCKS'] = '0'
		foreach ($argument in @('-C', $script:Root, '--no-pager') + $arguments) { [void] $start.ArgumentList.Add($argument) }
		$process = [Diagnostics.Process]::new()
		$process.StartInfo = $start
		if (-not $process.Start()) { throw 'Could not start git for the diff evidence.' }
		# The child's diff bytes go straight from its pipe into the prompt file: never into a
		# PowerShell variable, and never onto this script's stdout.
		$stderrTask = $process.StandardError.ReadToEndAsync()
		Add-PromptStream $process.StandardOutput.BaseStream $process
		$process.WaitForExit()
		$exitCode = $process.ExitCode
		$stderr = $stderrTask.GetAwaiter().GetResult()
		$process.Dispose()
		if ($exitCode -ne 0) {
			if (-not [string]::IsNullOrWhiteSpace($stderr)) { Write-PromptStderr $stderr }
			Complete-CodexReviewPrompt 1 'error' 'prompt.diff-failed' "git diff failed with exit $exitCode while streaming the evidence."
		}
		Add-PromptText "`n"
	}
	foreach ($entry in $script:Untracked) {
		if ($entry.Binary) { continue }
		Add-PromptText "## Untracked file: $($entry.Path)`n`n"
		$stream = [IO.File]::Open((Get-PromptFullPath $entry.Path), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
		try { Add-PromptStream $stream $null }
		finally { $stream.Dispose() }
		Add-PromptText "`n"
	}
}

try {
	$listed = [Collections.Generic.List[string]]::new()
	# `pwsh -File` hands one literal string per token, so the documented comma-separated form is
	# split here rather than by the parameter binder.
	foreach ($path in @(@($UntrackedPath) -split ',')) {
		if ([string]::IsNullOrWhiteSpace($path)) { continue }
		$listed.Add($path.Trim())
	}
	if ($listed.Count -gt 0 -and -not [string]::IsNullOrWhiteSpace($Head)) {
		Complete-CodexReviewPrompt 2 'blocked' 'prompt.head-untracked-conflict' 'A commit-valued -Head has no untracked side, so -UntrackedPath cannot be classified; drop one of the two.'
	}
	$script:Root = Get-AgentCanonicalPath $RepositoryRoot
	if (-not [IO.Path]::IsPathRooted($RepositoryRoot) -or -not (Test-Path -LiteralPath $script:Root -PathType Container)) {
		Complete-CodexReviewPrompt 2 'blocked' 'prompt.repository-root-invalid' "-RepositoryRoot must be an existing absolute directory: '$RepositoryRoot'."
	}
	if (-not (Test-Path -LiteralPath $ScopeFile -PathType Leaf)) {
		Complete-CodexReviewPrompt 1 'error' 'prompt.scope-file-missing' "-ScopeFile must be an existing file holding the manager-authored scope text: '$ScopeFile'."
	}
	$script:PromptFile = [IO.Path]::GetFullPath($PromptPath)
	if (Test-Path -LiteralPath $script:PromptFile) {
		Complete-CodexReviewPrompt 2 'blocked' 'prompt.path-exists' "-PromptPath already exists and is never overwritten: '$($script:PromptFile)'."
	}
	if (-not (Test-Path -LiteralPath $script:Template -PathType Leaf)) {
		Complete-CodexReviewPrompt 1 'error' 'prompt.template-invalid' "The prompt template is missing: '$($script:Template)'."
	}
	$templateText = [IO.File]::ReadAllText($script:Template)
	$roleInstruction = (Get-PromptFragment $templateText 'role-instruction').Replace('{{ASSIGNED_SKILL}}', $AssignedSkill)
	$guardrails = Get-PromptFragment $templateText 'guardrails'
	$outputContract = Get-PromptFragment $templateText 'output-contract'
	$scopeText = [IO.File]::ReadAllText($ScopeFile)

	$inventory = Get-ChangedFileSet $listed.ToArray()
	$listedSet = [Collections.Generic.HashSet[string]]::new([string[]] @($listed | ForEach-Object { Get-PromptRelativePath $_ }))
	$diffPath = [Collections.Generic.List[string]]::new()
	$untracked = [Collections.Generic.List[object]]::new()
	$fileLine = [Collections.Generic.List[string]]::new()
	$binaryExcluded = 0
	foreach ($entry in @($inventory.entries)) {
		if ($null -eq $entry.baseline -and $listedSet.Contains($entry.path)) {
			# The inventory classifies by extension first; the content probe is what keeps a binary
			# payload with a textual name out of the prompt.
			$isBinary = $entry.class -ceq 'binary' -or (Test-PromptBinaryContent $entry.path)
			if ($isBinary) { $binaryExcluded++ }
			$untracked.Add([pscustomobject] @{ Path = $entry.path; Binary = $isBinary })
			$fileLine.Add("$($entry.status) $($entry.path) — untracked$(if ($isBinary) { ', binary: path and status only' } else { ', contents below' })")
			continue
		}
		# A rename contributes both sides to the pathspec so both appear in the evidence.
		$diffPath.Add($entry.path)
		if ($null -ne $entry.oldPath) {
			$diffPath.Add($entry.oldPath)
			$fileLine.Add("$($entry.status) $($entry.path) — was $($entry.oldPath)")
			continue
		}
		$fileLine.Add("$($entry.status) $($entry.path)")
	}
	# A named path the inventory never reported — ignored, already tracked and unchanged, or absent —
	# would leave the prompt silently missing evidence the manager believes it carries.
	$untrackedSet = [Collections.Generic.HashSet[string]]::new([string[]] @($untracked | ForEach-Object { $_.Path }))
	$missing = @($listedSet | Where-Object { -not $untrackedSet.Contains($_) })
	if ($missing.Count -gt 0) {
		Complete-CodexReviewPrompt 2 'blocked' 'prompt.untracked-path-unknown' "The inventory reports no untracked entry for: $(($missing | Sort-Object) -join ', ')"
	}
	$script:DiffPath = $diffPath.ToArray()
	$script:Untracked = $untracked.ToArray()
	$script:DiffRange = if ([string]::IsNullOrEmpty($inventory.headSha)) { @($inventory.baselineSha) } else { @($inventory.baselineSha, $inventory.headSha) }

	$script:PromptStream = [IO.File]::Open($script:PromptFile, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
	$script:PromptCreated = $true

	Write-PromptSection '(a) Role' ($roleInstruction + "`n`n")
	Write-GuardrailBlock $guardrails

	$scopeBody = ''
	if ($RiskTier -ne 0) { $scopeBody += "Risk tier: $RiskTier`n`n" }
	$scopeBody += $scopeText
	if (-not $scopeBody.EndsWith("`n")) { $scopeBody += "`n" }
	Write-PromptSection '(b) Scope' $scopeBody

	$headText = if ([string]::IsNullOrEmpty($inventory.headSha)) { 'working tree' } else { $inventory.headSha }
	$evidence = "Baseline: $($inventory.baselineSha)`nHead: $headText`nChanged files ($($fileLine.Count)):`n"
	foreach ($line in $fileLine) { $evidence += "- $line`n" }
	$evidence += "`n"
	Write-PromptSection '(c) Evidence' $evidence
	Write-DiffEvidence

	Write-PromptSection '(d) Output contract' ($outputContract + "`n")

	$result.fileCount = $fileLine.Count
	$result.binaryExcluded = $binaryExcluded
	Complete-CodexReviewPrompt 0 'pass' 'ok' "Wrote a $($script:SectionCount)-section review prompt covering $($fileLine.Count) changed file(s)."
}
catch {
	Complete-CodexReviewPrompt 1 'error' 'internal.error' $_.Exception.Message
}

# Deterministic fixtures for New-CodexReviewPrompt.ps1 against throwaway Git repositories under the
# operating system temp directory. Every repository, scope file, and prompt path is created and
# removed by this harness, and no case points the script at a real checkout. Scope files and prompt
# paths live outside the fixture repository, because an unnamed untracked path inside it is itself a
# blocked result. Exit 0 means every case passed.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:Failures = [Collections.Generic.List[string]]::new()
$script:Roots = [Collections.Generic.List[string]]::new()
$script:Utf8 = [Text.UTF8Encoding]::new($false)
$script:Script = Join-Path $PSScriptRoot 'New-CodexReviewPrompt.ps1'
$script:Template = Join-Path $PSScriptRoot '..\references\prompt-template.md'
$script:PwshPath = (Get-Process -Id $PID).Path
$script:Scratch = Join-Path ([IO.Path]::GetTempPath()) ('codex-prompt-scratch-' + [Guid]::NewGuid().ToString('n').Substring(0, 8))

function Assert-True([bool] $Condition, [string] $Name) {
	if (-not $Condition) { $script:Failures.Add($Name); Write-Host "FAIL $Name" } else { Write-Host "pass $Name" }
}

function Assert-Equal($Expected, $Actual, [string] $Name) {
	Assert-True ("$Expected" -ceq "$Actual") "$Name (expected '$Expected', was '$Actual')"
}

function Get-FragmentText([string] $Name) {
	$text = [IO.File]::ReadAllText($script:Template)
	$pattern = "(?s)<!-- fragment: $([Regex]::Escape($Name)) -->\r?\n(.*?)\r?\n<!-- end-fragment: $([Regex]::Escape($Name)) -->"
	$match = [Regex]::Match($text, $pattern)
	if (-not $match.Success) { throw "The template has no '$Name' fragment." }
	return $match.Groups[1].Value.Replace("`r`n", "`n")
}

function Get-PromptSectionBody([string] $Prompt, [string] $Heading, [string] $NextBoundary) {
	# Extraction between the prompt format's own fixed boundaries, so extra or overriding text placed
	# next to a fragment lands inside the returned block and fails the exact comparison. An empty
	# boundary means the block runs to the end of the prompt.
	$startMarker = "# $Heading`n`n"
	$start = $Prompt.IndexOf($startMarker, [StringComparison]::Ordinal)
	if ($start -lt 0) { return $null }
	$body = $Prompt.Substring($start + $startMarker.Length)
	if ([string]::IsNullOrEmpty($NextBoundary)) { return $body }
	$end = $body.IndexOf($NextBoundary, [StringComparison]::Ordinal)
	if ($end -lt 0) { return $null }
	return $body.Substring(0, $end)
}

function New-FixtureRoot([string] $Name) {
	$root = Join-Path ([IO.Path]::GetTempPath()) ("codex-prompt-fixture-$Name-" + [Guid]::NewGuid().ToString('n').Substring(0, 8))
	[void] (New-Item -ItemType Directory -Path $root -Force)
	$script:Roots.Add($root)
	Invoke-FixtureGit $root @('init', '--quiet', '--initial-branch=main')
	Invoke-FixtureGit $root @('config', 'user.name', 'Fixture')
	Invoke-FixtureGit $root @('config', 'user.email', 'fixture@example.invalid')
	Invoke-FixtureGit $root @('config', 'core.autocrlf', 'false')
	Invoke-FixtureGit $root @('config', 'commit.gpgsign', 'false')
	return $root
}

function Get-FixtureGitText([string] $Root, [string[]] $Arguments) {
	$output = @(& git -C $Root --no-pager @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed in '$Root': $($output -join '; ')" }
	return ($output -join "`n")
}

function Invoke-FixtureGit([string] $Root, [string[]] $Arguments) {
	[void] (Get-FixtureGitText $Root $Arguments)
}

function Set-FixtureText([string] $Root, [string] $RelativePath, [string] $Text) {
	$full = Join-Path $Root ($RelativePath -replace '/', [IO.Path]::DirectorySeparatorChar)
	[void] (New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($full)) -Force)
	[IO.File]::WriteAllBytes($full, $script:Utf8.GetBytes($Text))
}

function Set-FixtureBytes([string] $Root, [string] $RelativePath, [byte[]] $Bytes) {
	$full = Join-Path $Root ($RelativePath -replace '/', [IO.Path]::DirectorySeparatorChar)
	[void] (New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($full)) -Force)
	[IO.File]::WriteAllBytes($full, $Bytes)
}

function New-ScratchFile([string] $Name, [string] $Text) {
	$path = Join-Path $script:Scratch ("$Name-" + [Guid]::NewGuid().ToString('n').Substring(0, 8) + '.md')
	[IO.File]::WriteAllBytes($path, $script:Utf8.GetBytes($Text))
	return $path
}

function New-ScratchPath([string] $Name) {
	return Join-Path $script:Scratch ("$Name-" + [Guid]::NewGuid().ToString('n').Substring(0, 8) + '.prompt.md')
}

function Invoke-PromptScript([string[]] $Arguments) {
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = $script:PwshPath
	$start.WorkingDirectory = [IO.Path]::GetTempPath()
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	$start.StandardOutputEncoding = $script:Utf8
	$start.StandardErrorEncoding = $script:Utf8
	foreach ($argument in @('-NoProfile', '-File', $script:Script) + $Arguments) { [void] $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw 'Could not start pwsh for the prompt script.' }
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$run = [ordered]@{
		ExitCode = $process.ExitCode
		Stdout = $stdoutTask.GetAwaiter().GetResult()
		Stderr = $stderrTask.GetAwaiter().GetResult()
		Json = $null
	}
	$process.Dispose()
	if (-not [string]::IsNullOrWhiteSpace($run.Stdout)) { try { $run.Json = $run.Stdout | ConvertFrom-Json -Depth 32 } catch { } }
	return [pscustomobject] $run
}

# --- Repository A: the assembled prompt ---------------------------------------------------------

$script:BinaryMarker = 'BINARY-PAYLOAD-MARKER'
$script:ScopeText = "Files and regions: Engine/Source/Keep.cpp lines 1-5.`nFocus: the changed bytes only.`nResiduals: none.`n"

function New-RepositoryA() {
	$root = New-FixtureRoot 'prompt'
	Set-FixtureText $root 'Engine/Source/Keep.cpp' "1`n2`n3`n4`n5`n"
	Set-FixtureText $root 'Engine/Source/Old.cpp' "int Old() { return 1; }`n"
	Invoke-FixtureGit $root @('add', '--all')
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'baseline')
	$baseline = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	Set-FixtureText $root 'Engine/Source/Keep.cpp' "1`n2`nthree`n4`n5`n"
	Invoke-FixtureGit $root @('mv', 'Engine/Source/Old.cpp', 'Engine/Source/New.cpp')
	Set-FixtureText $root 'Notes.md' "untracked note line one`nuntracked note line two`n"
	Set-FixtureBytes $root 'Tools/Blob.bin' ($script:Utf8.GetBytes($script:BinaryMarker) + [byte[]] @(0, 1, 2, 0, 255))
	# A binary payload carrying a textual extension: only a content probe keeps it out of the prompt.
	Set-FixtureBytes $root 'Docs/Capture.md' ($script:Utf8.GetBytes($script:BinaryMarker) + [byte[]] @(0, 7, 0, 9))
	return [pscustomobject] @{ Root = $root; Baseline = $baseline }
}

function Test-AssembledPrompt($Fixture) {
	$scopeFile = New-ScratchFile 'scope' $script:ScopeText
	$promptPath = New-ScratchPath 'assembled'
	$run = Invoke-PromptScript @(
		'-RepositoryRoot', $Fixture.Root, '-Baseline', $Fixture.Baseline, '-AssignedSkill', 'repo-code-review',
		'-ScopeFile', $scopeFile, '-PromptPath', $promptPath,
		'-UntrackedPath', 'Notes.md,Tools/Blob.bin,Docs/Capture.md')
	Assert-Equal 0 $run.ExitCode 'assembled exit code'
	Assert-True ([string]::IsNullOrEmpty($run.Stderr)) 'assembled writes nothing to stderr'
	Assert-True (-not $run.Stdout.Contains('diff --git')) 'assembled keeps the diff off stdout'
	Assert-True ($script:Utf8.GetByteCount($run.Stdout) -lt 4096) 'assembled stdout stays under 4 KB'
	if ($null -eq $run.Json) { Assert-True $false 'assembled emitted JSON'; return }
	Assert-Equal 'broken-engine-codex-review-prompt/v1' $run.Json.schemaVersion 'assembled schemaVersion'
	Assert-Equal 'pass' $run.Json.status 'assembled status'
	Assert-Equal 'ok' $run.Json.code 'assembled code'
	Assert-True (-not [string]::IsNullOrWhiteSpace($run.Json.message)) 'assembled message is non-empty'
	Assert-Equal ([IO.Path]::GetFullPath($promptPath)) $run.Json.promptPath 'assembled promptPath'
	Assert-Equal 5 $run.Json.fileCount 'assembled fileCount'
	Assert-Equal 2 $run.Json.binaryExcluded 'assembled binaryExcluded'
	Assert-Equal 4 $run.Json.sectionsWritten 'assembled sectionsWritten'
	Assert-True (Test-Path -LiteralPath $promptPath) 'assembled wrote the prompt file'
	if (-not (Test-Path -LiteralPath $promptPath)) { return }
	$bytes = [IO.File]::ReadAllBytes($promptPath)
	Assert-Equal $bytes.Length $run.Json.promptBytes 'assembled promptBytes matches the file length'
	$prompt = $script:Utf8.GetString($bytes)

	$headings = @('# (a) Role', '# (b) Scope', '# (c) Evidence', '# (d) Output contract')
	$previous = -1
	foreach ($heading in $headings) {
		$index = $prompt.IndexOf($heading, [StringComparison]::Ordinal)
		Assert-True ($index -gt $previous) "assembled section order places '$heading' after the previous section"
		$previous = $index
	}
	$delimiters = @($prompt -split "`n" | Where-Object { $_ -ceq '---' })
	Assert-Equal 3 $delimiters.Count 'assembled separates the four sections with three --- delimiter lines'

	$role = (Get-FragmentText 'role-instruction').Replace('{{ASSIGNED_SKILL}}', 'repo-code-review')
	$sectionA = Get-PromptSectionBody $prompt '(a) Role' "`n---`n`n# (b) Scope`n`n"
	Assert-True ($null -ne $sectionA) 'assembled has a role section between its boundaries'
	if ($null -ne $sectionA) {
		$guardrailMarker = "## Guardrails`n`n"
		$guardrailIndex = $sectionA.IndexOf($guardrailMarker, [StringComparison]::Ordinal)
		Assert-True ($guardrailIndex -ge 0) 'assembled places the guardrail block inside the role section'
		if ($guardrailIndex -ge 0) {
			Assert-True ($sectionA.Substring(0, $guardrailIndex) -ceq ($role + "`n`n")) 'assembled copies the role instruction with the assigned skill substituted'
			Assert-True ($sectionA.Substring($guardrailIndex + $guardrailMarker.Length) -ceq ((Get-FragmentText 'guardrails') + "`n")) 'assembled copies the guardrail block byte-identically'
		}
	}
	Assert-True ((Get-PromptSectionBody $prompt '(d) Output contract' '') -ceq ((Get-FragmentText 'output-contract') + "`n")) 'assembled copies the output contract byte-identically'
	Assert-True ((Get-PromptSectionBody $prompt '(b) Scope' "`n---`n`n# (c) Evidence`n`n") -ceq $script:ScopeText) 'assembled copies the scope text byte-identically'
	Assert-True (-not $prompt.Contains('Risk tier:')) 'assembled omits the risk tier line when none is given'

	$expectedDiff = Get-FixtureGitText $Fixture.Root @('diff', '-M', '--no-color', '--no-ext-diff', $Fixture.Baseline, '--', 'Engine/Source/Keep.cpp', 'Engine/Source/New.cpp', 'Engine/Source/Old.cpp')
	$start = $prompt.IndexOf("## Diff`n`n", [StringComparison]::Ordinal)
	Assert-True ($start -ge 0) 'assembled has a diff block'
	if ($start -ge 0) {
		$body = $prompt.Substring($start + "## Diff`n`n".Length)
		$end = $body.IndexOf("`n---`n", [StringComparison]::Ordinal)
		if ($end -ge 0) { $body = $body.Substring(0, $end) }
		$actualDiff = $body.Substring(0, $body.IndexOf("## Untracked file:", [StringComparison]::Ordinal)).TrimEnd("`n")
		Assert-True ($actualDiff -ceq $expectedDiff.TrimEnd("`n")) 'assembled diff equals git diff for the inventoried paths'
		Assert-True ($actualDiff.Contains('rename from Engine/Source/Old.cpp')) 'assembled diff carries both sides of a rename'
	}

	Assert-True ($prompt.Contains("## Untracked file: Notes.md")) 'assembled inlines the untracked text file'
	Assert-True ($prompt.Contains('untracked note line two')) 'assembled inlines the untracked text contents'
	Assert-True (-not $prompt.Contains('## Untracked file: Tools/Blob.bin')) 'assembled never inlines an untracked binary file'
	Assert-True (-not $prompt.Contains($script:BinaryMarker)) 'assembled keeps binary bytes out of the prompt, including a binary file named .md'
	Assert-True ($prompt.Contains('A Tools/Blob.bin — untracked, binary: path and status only')) 'assembled lists the untracked binary by path and status'
	Assert-True ($prompt.Contains('A Docs/Capture.md — untracked, binary: path and status only')) 'assembled classifies a binary .md by content, not extension'
}

function Test-RiskTierLine($Fixture) {
	$scopeFile = New-ScratchFile 'scope' $script:ScopeText
	$promptPath = New-ScratchPath 'risktier'
	$run = Invoke-PromptScript @(
		'-RepositoryRoot', $Fixture.Root, '-Baseline', $Fixture.Baseline, '-AssignedSkill', 'scope-review',
		'-ScopeFile', $scopeFile, '-PromptPath', $promptPath, '-RiskTier', '3',
		'-UntrackedPath', 'Notes.md,Tools/Blob.bin,Docs/Capture.md')
	Assert-Equal 0 $run.ExitCode 'risk tier exit code'
	if (-not (Test-Path -LiteralPath $promptPath)) { Assert-True $false 'risk tier wrote the prompt file'; return }
	$prompt = $script:Utf8.GetString([IO.File]::ReadAllBytes($promptPath))
	Assert-True ((Get-PromptSectionBody $prompt '(b) Scope' "`n---`n`n# (c) Evidence`n`n") -ceq ("Risk tier: 3`n`n" + $script:ScopeText)) 'risk tier writes one line above the verbatim scope text'
}

function Test-PromptPathExists($Fixture) {
	$scopeFile = New-ScratchFile 'scope' $script:ScopeText
	$promptPath = New-ScratchPath 'existing'
	$existing = "already here`n"
	[IO.File]::WriteAllBytes($promptPath, $script:Utf8.GetBytes($existing))
	$run = Invoke-PromptScript @(
		'-RepositoryRoot', $Fixture.Root, '-Baseline', $Fixture.Baseline, '-AssignedSkill', 'repo-code-review',
		'-ScopeFile', $scopeFile, '-PromptPath', $promptPath,
		'-UntrackedPath', 'Notes.md,Tools/Blob.bin,Docs/Capture.md')
	Assert-Equal 2 $run.ExitCode 'existing prompt path exit code'
	if ($null -ne $run.Json) {
		Assert-Equal 'blocked' $run.Json.status 'existing prompt path status'
		Assert-Equal 'prompt.path-exists' $run.Json.code 'existing prompt path code'
		Assert-True ($null -eq $run.Json.promptPath) 'existing prompt path reports no written prompt'
	}
	else { Assert-True $false 'existing prompt path emitted JSON' }
	Assert-True ($script:Utf8.GetString([IO.File]::ReadAllBytes($promptPath)) -ceq $existing) 'existing prompt path leaves the file byte-unchanged'
}

function Test-ScopeFileMissing($Fixture) {
	$promptPath = New-ScratchPath 'noscope'
	$run = Invoke-PromptScript @(
		'-RepositoryRoot', $Fixture.Root, '-Baseline', $Fixture.Baseline, '-AssignedSkill', 'repo-code-review',
		'-ScopeFile', (Join-Path $script:Scratch 'no-such-scope.md'), '-PromptPath', $promptPath)
	Assert-Equal 1 $run.ExitCode 'missing scope file exit code'
	if ($null -ne $run.Json) {
		Assert-Equal 'error' $run.Json.status 'missing scope file status'
		Assert-Equal 'prompt.scope-file-missing' $run.Json.code 'missing scope file code'
	}
	else { Assert-True $false 'missing scope file emitted JSON' }
	Assert-True (-not (Test-Path -LiteralPath $promptPath)) 'missing scope file creates no prompt file'
}

function Test-UnlistedUntracked($Fixture) {
	$scopeFile = New-ScratchFile 'scope' $script:ScopeText
	$promptPath = New-ScratchPath 'unlisted'
	$run = Invoke-PromptScript @(
		'-RepositoryRoot', $Fixture.Root, '-Baseline', $Fixture.Baseline, '-AssignedSkill', 'repo-code-review',
		'-ScopeFile', $scopeFile, '-PromptPath', $promptPath, '-UntrackedPath', 'Notes.md')
	Assert-Equal 2 $run.ExitCode 'unlisted untracked exit code'
	if ($null -ne $run.Json) {
		Assert-Equal 'blocked' $run.Json.status 'unlisted untracked status'
		Assert-Equal 'prompt.inventory-truncated' $run.Json.code 'unlisted untracked code'
	}
	else { Assert-True $false 'unlisted untracked emitted JSON' }
	Assert-True (-not (Test-Path -LiteralPath $promptPath)) 'unlisted untracked creates no prompt file'
}

# --- Repository B: a committed head over a dirty working tree ------------------------------------

function Test-HeadHonoured() {
	$root = New-FixtureRoot 'head'
	Set-FixtureText $root 'Engine/Source/Head.cpp' "baseline content`n"
	Invoke-FixtureGit $root @('add', '--all')
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'baseline')
	$baseline = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	Set-FixtureText $root 'Engine/Source/Head.cpp' "committed head content`n"
	Invoke-FixtureGit $root @('add', '--all')
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'head')
	$head = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	Set-FixtureText $root 'Engine/Source/Head.cpp' "dirty working tree content`n"

	$scopeFile = New-ScratchFile 'scope' $script:ScopeText
	$promptPath = New-ScratchPath 'head'
	$run = Invoke-PromptScript @(
		'-RepositoryRoot', $root, '-Baseline', $baseline, '-AssignedSkill', 'repo-code-review',
		'-ScopeFile', $scopeFile, '-PromptPath', $promptPath, '-Head', $head)
	Assert-Equal 0 $run.ExitCode 'head-side exit code'
	if (-not (Test-Path -LiteralPath $promptPath)) { Assert-True $false 'head-side wrote the prompt file'; return }
	$prompt = $script:Utf8.GetString([IO.File]::ReadAllBytes($promptPath))
	Assert-True ($prompt.Contains("Head: $head")) 'head-side names the resolved head SHA'
	Assert-True ($prompt.Contains('committed head content')) 'head-side diffs the committed head'
	Assert-True (-not $prompt.Contains('dirty working tree content')) 'head-side ignores dirty working-tree bytes'

	$conflictPrompt = New-ScratchPath 'conflict'
	$conflict = Invoke-PromptScript @(
		'-RepositoryRoot', $root, '-Baseline', $baseline, '-AssignedSkill', 'repo-code-review',
		'-ScopeFile', $scopeFile, '-PromptPath', $conflictPrompt, '-Head', $head, '-UntrackedPath', 'Notes.md')
	Assert-Equal 2 $conflict.ExitCode 'head with untracked exit code'
	if ($null -ne $conflict.Json) {
		Assert-Equal 'blocked' $conflict.Json.status 'head with untracked status'
		Assert-Equal 'prompt.head-untracked-conflict' $conflict.Json.code 'head with untracked code'
	}
	else { Assert-True $false 'head with untracked emitted JSON' }
	Assert-True (-not (Test-Path -LiteralPath $conflictPrompt)) 'head with untracked creates no prompt file'
}

# --- Repository C: the 4 MB prompt budget --------------------------------------------------------

function Test-DiffTooLarge() {
	$root = New-FixtureRoot 'oversize'
	Set-FixtureText $root 'Engine/Source/Small.cpp' "small`n"
	Invoke-FixtureGit $root @('add', '--all')
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'baseline')
	$baseline = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	Set-FixtureText $root 'Engine/Source/Small.cpp' "small`nchanged`n"
	$builder = [Text.StringBuilder]::new()
	# Just over the 4 MB prompt budget on its own, so only the budget rule can decide this case.
	for ($index = 0; $index -lt 150000; $index++) { [void] $builder.Append("oversized evidence line $index`n") }
	Set-FixtureText $root 'Huge.txt' $builder.ToString()

	$scopeFile = New-ScratchFile 'scope' $script:ScopeText
	$promptPath = New-ScratchPath 'oversize'
	$run = Invoke-PromptScript @(
		'-RepositoryRoot', $root, '-Baseline', $baseline, '-AssignedSkill', 'repo-code-review',
		'-ScopeFile', $scopeFile, '-PromptPath', $promptPath, '-UntrackedPath', 'Huge.txt')
	Assert-Equal 2 $run.ExitCode 'oversize exit code'
	if ($null -ne $run.Json) {
		Assert-Equal 'blocked' $run.Json.status 'oversize status'
		Assert-Equal 'prompt.diff-too-large' $run.Json.code 'oversize code'
		Assert-True ($run.Json.message.Contains('Huge.txt')) 'oversize message names the largest contributing path'
		Assert-Equal 0 $run.Json.promptBytes 'oversize reports no prompt bytes'
	}
	else { Assert-True $false 'oversize emitted JSON' }
	Assert-True (-not (Test-Path -LiteralPath $promptPath)) 'oversize leaves no partial prompt file'
	Assert-True (-not $run.Stdout.Contains('oversized evidence line')) 'oversize keeps evidence off stdout'
}

try {
	[void] (New-Item -ItemType Directory -Path $script:Scratch -Force)
	$repositoryA = New-RepositoryA
	Test-AssembledPrompt $repositoryA
	Test-RiskTierLine $repositoryA
	Test-PromptPathExists $repositoryA
	Test-ScopeFileMissing $repositoryA
	Test-UnlistedUntracked $repositoryA
	Test-HeadHonoured
	Test-DiffTooLarge
}
finally {
	foreach ($root in $script:Roots) {
		if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue }
	}
	if (Test-Path -LiteralPath $script:Scratch) { Remove-Item -LiteralPath $script:Scratch -Recurse -Force -ErrorAction SilentlyContinue }
}

if ($script:Failures.Count -gt 0) {
	Write-Host "FAILED: $($script:Failures.Count) fixture assertion(s)."
	exit 1
}

Write-Host 'All Codex review prompt fixtures passed.'
exit 0

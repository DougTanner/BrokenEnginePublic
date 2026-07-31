# Writes one executable Plan file with its byte-zero metadata marker and validates the tree afterwards.
# The body arrives as a file path so no transcript text is ever executed, and it is written verbatim:
# this script decides nothing about plan content.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $Area,
	[Parameter(Mandatory)][string] $Name,
	[Parameter(Mandatory)][string] $Body,
	[string[]] $DependsOn = @()
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'AgentScriptCommon.psm1') -Force -DisableNameChecking

$script:MaximumDependsOn = 64
$script:MaximumDiagnostics = 16
$script:MaximumMessageLength = 256
$script:MaximumOutputBytes = 8192

$result = [ordered]@{
	schemaVersion = 'broken-engine-new-plan-file/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Plan file creation did not run.'
	plan = $null
	createdUtc = $null
	dependsOn = @()
	written = $false
	validation = $null
	truncated = $false
}

function Write-PlanFileDiagnostic([string] $Message) { [Console]::Error.WriteLine("NewPlanFile: $Message") }

function Limit-PlanFileText([string] $Value) {
	if ($null -eq $Value) { return '' }
	$text = $Value.Trim()
	if ($text.Length -le $script:MaximumMessageLength) { return $text }
	$result.truncated = $true
	return $text.Substring(0, $script:MaximumMessageLength)
}

function Complete-PlanFile([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = Limit-PlanFileText $Message
	$json = $result | ConvertTo-Json -Depth 32 -Compress
	if ([Text.Encoding]::UTF8.GetByteCount($json) -gt $script:MaximumOutputBytes) {
		# Only the validation detail can grow without bound; drop it rather than emit an unbounded envelope.
		$result.validation = $null
		$result.truncated = $true
		$json = $result | ConvertTo-Json -Depth 32 -Compress
	}
	[Console]::Out.Write($json)
	exit $ExitCode
}

function Get-PlanFileRelativePath([string] $FullPath, [string] $Root) {
	$rootWithSeparator = $Root.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
	return $FullPath.Substring($rootWithSeparator.Length).Replace([IO.Path]::DirectorySeparatorChar, '/').Replace([IO.Path]::AltDirectorySeparatorChar, '/')
}

function Test-PlanFileContained([string] $FullPath, [string] $Root) {
	$rootWithSeparator = $Root.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
	return $FullPath.StartsWith($rootWithSeparator, [StringComparison]::OrdinalIgnoreCase)
}

function Invoke-PlanFileProcess([string] $Executable, [string[]] $Arguments, [string] $WorkingDirectory) {
	$encoding = [Text.UTF8Encoding]::new($false)
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = $Executable
	$start.WorkingDirectory = $WorkingDirectory
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	$start.StandardOutputEncoding = $encoding
	$start.StandardErrorEncoding = $encoding
	foreach ($argument in $Arguments) { [void] $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw "Could not start '$Executable'." }
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$response = [pscustomobject]@{ ExitCode = $process.ExitCode; Stdout = $stdoutTask.GetAwaiter().GetResult(); Stderr = $stderrTask.GetAwaiter().GetResult() }
	$process.Dispose()
	return $response
}

try {
	$repositoryRoot = Get-AgentCanonicalPath (Join-Path $PSScriptRoot '..\..')
	$plansRoot = Get-AgentCanonicalPath (Join-Path $repositoryRoot 'Documents\Plans')

	if ([IO.Path]::GetFileName($Name) -cne $Name -or $Name -cmatch '[\\/]') {
		Complete-PlanFile 1 'error' 'input.name-directory-component' "Plan -Name must be a bare filename with no directory component: '$Name'."
	}
	if ($Name -cnotmatch '^[A-Z][A-Za-z0-9]*\.md$') {
		Complete-PlanFile 1 'error' 'input.name-invalid' "Plan -Name must match '^[A-Z][A-Za-z0-9]*\.md$': '$Name'."
	}

	$areaInput = $Area.Replace('\', '/').Trim()
	$areaFull = if ([IO.Path]::IsPathFullyQualified($areaInput)) {
		Get-AgentCanonicalPath $areaInput
	} elseif ($areaInput -ceq 'Documents/Plans' -or $areaInput.StartsWith('Documents/Plans/', [StringComparison]::Ordinal)) {
		Get-AgentCanonicalPath (Join-Path $repositoryRoot $areaInput)
	} else {
		Get-AgentCanonicalPath (Join-Path $plansRoot $areaInput)
	}
	if (-not (Test-PlanFileContained $areaFull $plansRoot)) {
		Complete-PlanFile 1 'error' 'input.area-outside-plans' "Plan -Area must resolve beneath Documents/Plans: '$Area'."
	}
	if (-not (Test-Path -LiteralPath $areaFull -PathType Container)) {
		Complete-PlanFile 1 'error' 'input.area-not-found' "Plan -Area must be an existing directory: '$Area'."
	}

	$targetFull = Join-Path $areaFull $Name
	if (Test-Path -LiteralPath $targetFull) {
		Complete-PlanFile 1 'error' 'target.exists' "A path already exists at the plan target and is never overwritten: '$(Get-PlanFileRelativePath $targetFull $repositoryRoot)'."
	}
	$result.plan = Get-PlanFileRelativePath $targetFull $repositoryRoot

	$bodyFull = if ([IO.Path]::IsPathFullyQualified($Body)) { Get-AgentCanonicalPath $Body } else { Get-AgentCanonicalPath (Join-Path $repositoryRoot $Body) }
	if (-not (Test-Path -LiteralPath $bodyFull -PathType Leaf)) {
		Complete-PlanFile 1 'error' 'input.body-not-found' "Plan -Body must be an existing file holding the plan text below the marker: '$Body'."
	}

	$normalized = [Collections.Generic.List[string]]::new()
	# `pwsh -File` hands every argument over as one literal string, so the required invocation form can
	# only deliver several dependencies as one comma-separated token.
	foreach ($dependency in (@($DependsOn) | ForEach-Object { $_ -split ',' })) {
		if ([string]::IsNullOrWhiteSpace($dependency)) {
			Complete-PlanFile 1 'error' 'input.depends-on-invalid' 'Every -DependsOn entry must be a Documents/Plans/**/*.md path; a blank entry is not one.'
		}
		$dependencyInput = $dependency.Replace('\', '/').Trim()
		$dependencyFull = if ([IO.Path]::IsPathFullyQualified($dependencyInput)) { Get-AgentCanonicalPath $dependencyInput } else { Get-AgentCanonicalPath (Join-Path $repositoryRoot $dependencyInput) }
		if (-not (Test-PlanFileContained $dependencyFull $plansRoot) -or [IO.Path]::GetExtension($dependencyFull) -cne '.md') {
			Complete-PlanFile 1 'error' 'input.depends-on-invalid' "Every -DependsOn entry must be a Documents/Plans/**/*.md path: '$dependency'."
		}
		# Resolving a path preserves the caller's casing, so the canonical prefix is prepended rather than carried
		# over from the input; the remainder names a real file whose casing the caller owns.
		$relative = 'Documents/Plans/' + (Get-PlanFileRelativePath $dependencyFull $plansRoot)
		if (-not $normalized.Contains($relative)) { [void] $normalized.Add($relative) }
	}
	$normalized.Sort([StringComparer]::Ordinal)
	# Statement assignment, never an `if` expression: an if-expression sends its value through the
	# pipeline, which unrolls an empty list into a null payload.
	$reported = [string[]] @($normalized)
	if ($reported.Count -gt $script:MaximumDependsOn) {
		$result.truncated = $true
		$reported = [string[]] $reported[0..($script:MaximumDependsOn - 1)]
	}
	$result.dependsOn = $reported

	$createdUtc = [DateTime]::UtcNow.ToString("yyyy-MM-dd'T'HH:mm:ss.fff'Z'", [Globalization.CultureInfo]::InvariantCulture)
	$result.createdUtc = $createdUtc
	$dependsOnJson = (@($normalized) | ForEach-Object { ConvertTo-Json $_ -Compress }) -join ','
	$marker = '<!-- broken-engine-plan/v1 {"createdUtc":"' + $createdUtc + '","dependsOn":[' + $dependsOnJson + ']} -->'
	# The marker must occupy byte zero, so the file is written with an explicit BOM-less UTF-8 encoding.
	[IO.File]::WriteAllText($targetFull, $marker + "`n" + [IO.File]::ReadAllText($bodyFull), [Text.UTF8Encoding]::new($false))
	$result.written = $true

	$worktreeCli = Join-Path $repositoryRoot 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe'
	if (-not (Test-Path -LiteralPath $worktreeCli -PathType Leaf)) {
		Complete-PlanFile 2 'blocked' 'worktree-cli.missing' "The provisioned WorktreeCli is missing, so plan validation could not run: '$worktreeCli'."
	}
	$commonDirectory = (Invoke-AgentGit @('-C', $repositoryRoot, 'rev-parse', '--path-format=absolute', '--git-common-dir') | Select-Object -First 1).ToString().Trim()
	$validate = Invoke-PlanFileProcess $worktreeCli @('plan', 'validate', '--repo', $commonDirectory, '--worktree', $repositoryRoot) $repositoryRoot
	$validation = $null
	if (-not [string]::IsNullOrWhiteSpace($validate.Stdout)) {
		try { $validation = $validate.Stdout.Trim() | ConvertFrom-Json -Depth 32 -ErrorAction Stop } catch { Write-PlanFileDiagnostic "plan validate did not return one JSON value: $($_.Exception.Message)" }
	}
	$projection = [ordered]@{ exitCode = $validate.ExitCode }
	foreach ($name in @('status', 'code', 'message')) {
		if ($null -ne $validation -and $validation.PSObject.Properties.Name -ccontains $name) { $projection[$name] = Limit-PlanFileText ([string] $validation.$name) }
	}
	foreach ($name in @('diagnostics', 'notices')) {
		if ($null -ne $validation -and $validation.PSObject.Properties.Name -ccontains $name) {
			$entries = @($validation.$name)
			if ($entries.Count -gt $script:MaximumDiagnostics) { $result.truncated = $true; $entries = $entries[0..($script:MaximumDiagnostics - 1)] }
			$projection[$name] = $entries
		}
	}
	$result.validation = $projection
	if ($validate.ExitCode -ne 0) {
		$detail = Limit-PlanFileText ((($validate.Stderr, $validate.Stdout) -join ' ').Trim())
		Write-PlanFileDiagnostic "plan validate exited $($validate.ExitCode): $detail"
		# The written file stays in place so the caller can fix its body instead of retyping the plan.
		$exit = if ($validate.ExitCode -eq 2) { 2 } else { 1 }
		Complete-PlanFile $exit $(if ($exit -eq 2) { 'blocked' } else { 'error' }) 'plan.validation-failed' "Plan file was written but 'plan validate' rejected the tree; fix the body and revalidate."
	}
	Complete-PlanFile 0 'pass' 'ok' 'Plan file written and validated.'
}
catch {
	Write-PlanFileDiagnostic $_.Exception.Message
	Complete-PlanFile 1 'error' 'internal.error' $_.Exception.Message
}

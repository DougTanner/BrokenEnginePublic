<#
.SYNOPSIS
	Run a Broken Engine review skill on Codex (Sol) headless and capture its findings.

	Primary Claude Code reviewer route: it runs a delegated reviewer/auditor role on Codex/Sol.
	Driven by the /codex-review skill, which on failure falls back to the Opus reviewer subagent,
	then general-purpose on Opus. Codex never invokes this — its reviewer role is already Sol.

.NOTES
	Auth/billing: Codex uses ChatGPT sign-in by default -> ChatGPT subscription quota, NOT metered
	OpenAI API credits (verify with `codex login status`). The wrapper refuses an inherited
	OPENAI_API_KEY so the child cannot bill the API instead.

	Exit codes: passes through Codex's exit code; 126 if an inherited OPENAI_API_KEY is refused,
	127 if the codex CLI is not found (the driver treats any non-zero as CODEX-UNAVAILABLE and the
	skill falls back to the Opus reviewer subagent, then general-purpose on Opus).
#>
param(
	[Parameter(Mandatory)][string] $Worktree,    # session worktree checkout to review in (codex -C)
	[Parameter(Mandatory)][string] $PromptFile,  # file holding the assembled review prompt (fed on stdin)
	[Parameter(Mandatory)][string] $OutFile      # Codex writes its final message here (codex -o)
)

$ErrorActionPreference = 'Stop'

if ($null -ne [Environment]::GetEnvironmentVariable('OPENAI_API_KEY', 'Process'))
{
	Write-Error 'refusing to launch Codex with inherited OPENAI_API_KEY' -ErrorAction Continue
	exit 126
}

$codex = Get-Command codex -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $codex)
{
	Write-Error 'codex CLI not found on PATH' -ErrorAction Continue
	exit 127
}

$temporaryDirectory = [System.IO.Path]::GetTempPath()
$temporaryStem = "broken-engine-codex-review-$([guid]::NewGuid().ToString('N'))"
$temporaryPrompt = Join-Path $temporaryDirectory "$temporaryStem.prompt.md"
$temporaryOutput = Join-Path $temporaryDirectory "$temporaryStem.output.md"
$exitCode = 1

try
{
	Copy-Item -LiteralPath $PromptFile -Destination $temporaryPrompt
	Get-Content -LiteralPath $temporaryPrompt -Raw | & $codex.Source `
		-a never `
		exec `
		--sandbox read-only `
		-C $Worktree `
		-m gpt-5.6-sol `
		-c 'model_reasoning_effort="medium"' `
		--ephemeral `
		-o $temporaryOutput `
		-
	$exitCode = $LASTEXITCODE
	if ($exitCode -eq 0)
	{
		Copy-Item -LiteralPath $temporaryOutput -Destination $OutFile -Force
	}
}
finally
{
	Remove-Item -LiteralPath $temporaryPrompt, $temporaryOutput -Force -ErrorAction SilentlyContinue
}

exit $exitCode

<#
.SYNOPSIS
	Run a Broken Engine review skill on Codex (Sol) headless and capture its findings.

	Claude Code falls back here when Fable is unavailable or over limit (AGENTS.md global model-fallback
	rule): it runs a delegated reviewer/auditor role on Codex/Sol instead of Fable. Codex never invokes
	this — under the Fable->Sol mapping it is already Sol. Driven by the /codex-review skill.

.NOTES
	Auth/billing: Codex uses ChatGPT sign-in by default -> ChatGPT subscription quota, NOT metered
	OpenAI API credits (verify with `codex login status`). Do NOT export OPENAI_API_KEY into this
	process, or Codex would bill the API instead.

	Exit codes: passes through Codex's exit code; 127 if the codex CLI is not found (the driver
	treats any non-zero as CODEX-UNAVAILABLE and runs the role on Opus instead).
#>
param(
	[Parameter(Mandatory)][string] $Worktree,    # session worktree checkout to review in (codex -C)
	[Parameter(Mandatory)][string] $PromptFile,  # file holding the assembled review prompt (fed on stdin)
	[Parameter(Mandatory)][string] $OutFile      # Codex writes its final message here (codex -o)
)

$codex = Get-Command codex -CommandType Application -ErrorAction SilentlyContinue
if (-not $codex)
{
	Write-Error 'codex CLI not found on PATH'
	exit 127
}

Get-Content -LiteralPath $PromptFile -Raw | & $codex.Source exec `
	--dangerously-bypass-approvals-and-sandbox `
	-C $Worktree `
	-m gpt-5.6-sol `
	-c 'model_reasoning_effort="xhigh"' `
	--ephemeral `
	-o $OutFile `
	-

exit $LASTEXITCODE

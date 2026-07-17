Set-StrictMode -Version Latest

# Exports are Agent-prefixed so they can never shadow a host script's local Invoke-Git/Get-CanonicalPath
# when this module is imported by a script invoked in-process from that host (same convention as
# FinalizeWorkflowCommon's Finalize-prefixed exports).

function Get-AgentCanonicalPath([string] $Path) {
	$full = [System.IO.Path]::GetFullPath($Path)
	$root = [System.IO.Path]::GetPathRoot($full)
	if ($full.Length -gt $root.Length) { return $full.TrimEnd('\', '/') }
	return $full
}

function Invoke-AgentGit([string[]] $Arguments) {
	$output = @(& git @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $($output -join '; ')" }
	return $output
}

Export-ModuleMember -Function Get-AgentCanonicalPath, Invoke-AgentGit

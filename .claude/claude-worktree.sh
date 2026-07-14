#!/usr/bin/env bash
set -euo pipefail
repository_root="$(git rev-parse --show-toplevel)"
legacy_flag=()
if [[ "${1:-}" == "--legacy-sessions-closed" ]]; then
	legacy_flag=(-LegacySessionsClosed)
	shift
fi
exec pwsh -NoProfile -ExecutionPolicy Bypass -File "$repository_root/.agents/scripts/Start-AgentWorktreeSession.ps1" \
	-Client claude -RepositoryRoot "$(cygpath -w "$repository_root")" "${legacy_flag[@]}" \
	-ClientArguments "--dangerously-skip-permissions" "$@"

#!/usr/bin/env bash
set -euo pipefail
repository_root="$(git rev-parse --show-toplevel)"
legacy_flag=()
client_args=()
for argument in "$@"; do
	if [[ "$argument" == "--legacy-sessions-closed" ]]; then
		legacy_flag=(-LegacySessionsClosed)
	else
		client_args+=("$argument")
	fi
done
exec pwsh -NoProfile -ExecutionPolicy Bypass -File "$repository_root/.agents/scripts/Start-AgentWorktreeSession.ps1" \
	-Client claude -RepositoryRoot "$(cygpath -w "$repository_root")" "${legacy_flag[@]}" \
	-ClientArguments "--dangerously-skip-permissions" "${client_args[@]}"

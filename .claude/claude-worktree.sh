#!/usr/bin/env bash
set -euo pipefail
repository_root="$(git rev-parse --show-toplevel)"
legacy_flag=()
client_args=("--dangerously-skip-permissions")
for argument in "$@"; do
	if [[ "$argument" == "--legacy-sessions-closed" ]]; then
		legacy_flag=(-LegacySessionsClosed)
	else
		client_args+=("$argument")
	fi
done
# Carry client arguments out of band; passing them to pwsh -File would bind them to parameters of the
# session script instead of forwarding them. Start-AgentWorktreeSession.ps1 decodes and clears this.
# Assign on its own line: `export VAR="$(...)"` would discard a base64 failure under `set -e` and
# launch the client with no arguments at all, including no permission bypass.
client_arguments_encoded="$(printf '%s\0' "${client_args[@]}" | base64 -w0)"
export BROKEN_ENGINE_CLIENT_ARGUMENTS="$client_arguments_encoded"
exec pwsh -NoProfile -ExecutionPolicy Bypass -File "$repository_root/.agents/scripts/Start-AgentWorktreeSession.ps1" \
	-Client claude -RepositoryRoot "$(cygpath -w "$repository_root")" "${legacy_flag[@]}"

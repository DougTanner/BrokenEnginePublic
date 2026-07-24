#!/usr/bin/env bash
set -euo pipefail
repository_root="$(git rev-parse --show-toplevel)"
reattach_flag=()
client_args=("--dangerously-skip-permissions")
while (($#)); do
	case "$1" in
		--reattach-worktree)
			if (($# < 2)) || [[ -z "$2" ]]; then
				echo "--reattach-worktree requires a worktree path" >&2
				exit 2
			fi
			reattach_flag=(-ReattachWorktree "$(cygpath -w "$2")")
			shift 2
			;;
		--reattach-worktree=*)
			worktree_path="${1#--reattach-worktree=}"
			if [[ -z "$worktree_path" ]]; then
				echo "--reattach-worktree requires a worktree path" >&2
				exit 2
			fi
			reattach_flag=(-ReattachWorktree "$(cygpath -w "$worktree_path")")
			shift
			;;
		*)
			client_args+=("$1")
			shift
			;;
	esac
done
# Carry client arguments out of band; passing them to pwsh -File would bind them to parameters of the
# session script instead of forwarding them. Start-AgentWorktreeSession.ps1 decodes and clears this.
# Assign on its own line: `export VAR="$(...)"` would discard a base64 failure under `set -e` and
# launch the client with no arguments at all, including no permission bypass.
client_arguments_encoded="$(printf '%s\0' "${client_args[@]}" | base64 -w0)"
export BROKEN_ENGINE_CLIENT_ARGUMENTS="$client_arguments_encoded"
exec pwsh -NoProfile -ExecutionPolicy Bypass -File "$repository_root/.agents/scripts/Start-AgentWorktreeSession.ps1" \
	-Client claude -RepositoryRoot "$(cygpath -w "$repository_root")" "${reattach_flag[@]}"

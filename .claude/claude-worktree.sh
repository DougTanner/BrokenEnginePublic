#!/usr/bin/env bash
set -euo pipefail

repository_root="$(git rev-parse --show-toplevel)"
repository_name="$(basename "$repository_root")"
uuid="$(powershell.exe -NoProfile -Command '[guid]::NewGuid().ToString()' | tr -d '\r')"
branch_name="claude/$uuid"
worktree_root="${HOME}/.claude/worktrees/${repository_name}"
worktree_path="${worktree_root}/${uuid}"
target_branch="$(git -C "$repository_root" branch --show-current)"
baseline="$(git -C "$repository_root" rev-parse HEAD)"

mkdir -p "$worktree_root"
git -C "$repository_root" worktree add -b "$branch_name" "$worktree_path" "$baseline"
if ! powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$repository_root/.agents/scripts/Provision-WorktreeThirdParty.ps1" -RepositoryRoot "$(cygpath -w "$worktree_path")"; then
	echo "Provisioning failed; preserved worktree '$worktree_path' and branch '$branch_name' for recovery." >&2
	exit 1
fi

unset BROKEN_ENGINE_WORKTREE_PATH BROKEN_ENGINE_SESSION_BRANCH BROKEN_ENGINE_PRIMARY_CHECKOUT BROKEN_ENGINE_TARGET_BRANCH BROKEN_ENGINE_BASELINE
export BROKEN_ENGINE_WORKTREE_PATH="$(cygpath -w "$worktree_path")"
export BROKEN_ENGINE_SESSION_BRANCH="$branch_name"
export BROKEN_ENGINE_PRIMARY_CHECKOUT="$(cygpath -w "$repository_root")"
export BROKEN_ENGINE_TARGET_BRANCH="$target_branch"
export BROKEN_ENGINE_BASELINE="$baseline"
cd "$worktree_path"
exec claude --dangerously-skip-permissions

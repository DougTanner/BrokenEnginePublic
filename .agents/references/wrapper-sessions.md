# Wrapper Sessions

A wrapper session is started through `.claude/claude-worktree.sh` or
`.codex/codex-worktree.ps1` and owns an isolated worktree whose durable session
identity lives in its private-Git receipt.

## Receipt and reattach rules

- Retained wrapper sessions reattach only through the same wrapper with its
  explicit reattach worktree input: `-ReattachWorktree <path>` for Codex,
  `--reattach-worktree <path>` for Claude.
- The wrapper reads its private-Git receipt and restores the original session
  owner; missing, altered, moved, or legacy receipts fail closed.
- The single legitimate receipt rewrite is the wrapper-owned baseline re-parent
  performed under the exclusive receipt write lease (squash recovery); every
  other change to a receipt still fails closed.
- Never reconstruct receipt provenance or adopt an arbitrary worktree.

## Primary advance during a session

A primary advance after a claim voids nothing; the stale-baseline
`missing-plan-file` notice is non-blocking and reconciliation resolves it —
canonical rule: `../skills/next-plan/references/execution-gates.md`
"Primary advance".

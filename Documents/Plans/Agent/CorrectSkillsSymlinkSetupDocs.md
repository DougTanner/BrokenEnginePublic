# Correct Skills Symlink Setup Documentation

## Context

`README.md:27` reverses the repository's tracked skills layout: it says `.agents/skills` is a symlink to `.claude/skills`, but Git tracks `.agents/skills` as the real directory and tracks `.claude/skills` as a mode-`120000` symbolic link whose blob target is `../.agents/skills`. `README.md:31` consequently tells users recovering from a checkout without symlink support to run `git checkout -- .agents/skills`; that path contains regular tracked files, so the command does not recreate the broken `.claude/skills` link.

A clone created without symlink support can also retain repository-local `core.symlinks=false`. Git's installed documentation states that this setting deliberately checks symbolic links out as small plain files containing the link text. An isolated checkout confirmed that re-checking out the correct path while the setting remains false still produces a regular file; setting the clone's local `core.symlinks` to `true` before the checkout recreates the symbolic link.

The mismatch is present at fixed session baseline `8600d6f`. On a correct Windows checkout, `Get-Item -Force .agents/skills,.claude/skills` reports only `.claude/skills` as a `SymbolicLink` targeting `..\.agents\skills`, matching `git ls-files -s -- .claude/skills` and `git show 8600d6f:.claude/skills`.

## Design

- Correct the Git prerequisite text to identify `.claude/skills` as the tracked symlink and `.agents/skills` as its real target, while preserving the existing Developer Mode prerequisite.
- Change recovery to require enabling Developer Mode, opening a new terminal, running `git config core.symlinks true` in the affected clone, and only then running `git checkout -- .claude/skills`. The configuration command intentionally updates that repository's local `.git/config`, not the user's global Git configuration.
- Keep the explanation tool-neutral: `.agents/skills` is the repository's canonical shared skills directory, and `.claude/skills` exposes the same skills to Claude Code.

## Critical files

- `README.md:27` — reversed symlink direction and incorrect shared-skills explanation.
- `README.md:31` — recovery omits the repository-local `core.symlinks=true` prerequisite and targets the real directory instead of the tracked symlink.
- `.claude/skills` — read-only verification target; tracked mode `120000`, content `../.agents/skills`.

## Out of scope

- Changing the tracked symlink, skills directory layout, Codex/Claude configuration, or worktree helper scripts.
- Changing global Git configuration.
- Expanding the README's general Git, Developer Mode, submodule, or CLI setup guidance.
- Adding an automated setup validator.

## Acceptance criteria

- README accurately states that `.claude/skills` points to `../.agents/skills` and no longer describes `.agents/skills` as the link.
- Recovery preserves the Developer Mode and new-terminal prerequisites, runs `git config core.symlinks true` in the affected clone, then checks out `.claude/skills`.
- Verification confirms the repository-local `core.symlinks` value is `true` and agrees with Git mode `120000`, the `../.agents/skills` blob target, and a symlink-enabled Windows checkout.

## Notes

- Documentation-only setup correction. No runtime, build, determinism/CRC, replay, wire protocol, `kiVersion`/`.pack`, client/server guard, allocation-tracked, shader, or live agent-harness exposure.

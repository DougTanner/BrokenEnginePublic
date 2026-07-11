# Move Gaea 2 Agent Cache Outside Checkouts

## Context

The repository cache audit found one purposeful ignored cache payload left inside the Git tree: `.claude/skills/gaea2-shared/cache/gaea-version.txt` and `sample-fingerprint.json` (about 28 KiB in the primary checkout). `check_gaea_version.py` and `inspect_samples.py` derive that cache beside their scripts, so every worktree starts without the PC's established Gaea version/fingerprint baseline and creates another mutable cache under the checkout.

This cache belongs to the Gaea 2 agent tools, not DataPacker. It should be PC-global and persistent across worktrees, while DataPacker continues to own its separate `%TEMP%/DataPacker/<Project>` runtime cache.

## Design

- Add one small shared Python cache-path helper under `.claude/skills/gaea2-shared/scripts/` that resolves `%LOCALAPPDATA%/BrokenEngine/AgentCache/Gaea2`. Treat missing/unusable `LOCALAPPDATA` as an explicit filesystem trust-boundary error rather than silently falling back into the checkout.
- Update `check_gaea_version.py` and `inspect_samples.py` to use the shared resolver for `gaea-version.txt` and `sample-fingerprint.json`.
- On first access, migrate the legacy sibling `../cache/` files into the PC-global directory. Copy/replace via a temporary file and validate the destination before removing a legacy source. If both locations already contain a file, keep the PC-global file authoritative and warn on differing legacy content; never overwrite a newer shared baseline implicitly.
- Keep the legacy checkout cache ignored during the migration window, but rewrite the `.gitignore` comment to identify it as migration-only rather than the active location.
- Update `gaea2-load/SKILL.md`, `gaea2-shared/examples/README.md`, and the scripts' help/docstrings so they name the PC-global cache and no longer claim that fresh checkouts necessarily produce a first-run probe.
- Verify from two worktrees that both scripts resolve the same files, the second worktree reuses the established baseline, and neither worktree creates a new `.claude/skills/gaea2-shared/cache/` payload.

## Critical files

- `.claude/skills/gaea2-shared/scripts/check_gaea_version.py` — current checkout-relative `CACHE_DIR` and version-baseline lifecycle.
- `.claude/skills/gaea2-shared/scripts/inspect_samples.py` — current checkout-relative `FINGERPRINT_PATH` and fingerprint replacement path.
- `.claude/skills/gaea2-load/SKILL.md` — user-facing version-probe/cache contract.
- `.claude/skills/gaea2-shared/examples/README.md` — current `cache/sample-fingerprint.json` reference.
- `.gitignore` — legacy `.claude/skills/gaea2-shared/cache/` exclusion.

## Out of scope

- Moving or merging DataPacker's `%TEMP%/DataPacker/<Project>` cache.
- Changing Gaea version detection, sample fingerprint schema/diff behavior, bundled example policy, or terrain load/modify/save behavior.
- Synchronizing the cache between PCs or users.

## Acceptance criteria

- Both Gaea scripts use one `%LOCALAPPDATA%` cache path shared by every worktree on the PC.
- Existing checkout-local baselines migrate without data loss; a conflicting PC-global baseline is not silently overwritten.
- A fresh worktree observes the existing version/fingerprint baseline and does not create mutable files under `.claude/skills/gaea2-shared/cache/`.
- Skill/docs and `.gitignore` describe the active and legacy locations accurately.

## Notes

- Keep cache writes crash-safe: the version and fingerprint files are consumed across independent agent sessions.
- Invariant exposure: agent tooling only. No engine/DataPacker binary, `.pack`/`kiVersion`, runtime determinism/CRC, replay, network, client/server guard, or allocation-tracked-path exposure.


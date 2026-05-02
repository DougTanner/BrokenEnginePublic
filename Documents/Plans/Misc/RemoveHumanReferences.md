# Remove "Human" Player References

Source: follow-up from session that landed lead-targeting math in `PlayersCombat.cpp`. User stated during planning: "There should be no references in the codebase or documentation to a 'Human' controlled Player, this concept does not exist anymore (any references should be removed). There is a Flagship Player that the others follow but it is not human-controlled."

## Context

The legacy game model had one human-controlled player and AI wingmen. The current model has a Flagship Player (no human) plus follower players that mirror the flagship's input/intents. Stale documentation and possibly stale code identifiers still refer to the obsolete "human" concept and risk misleading future readers (including subagents reading CLAUDE.md context) about who drives input.

Known doc hits:

- `Projects/BrokenEngineSandbox/Source/CLAUDE.md` — line approximately containing "The Frame does not know which player is human vs AI".
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/CLAUDE.md` — lines containing "Player spaceships (1 human + AI wingmen)" and "no index is privileged as 'human'".

Code hits are likely none, but unverified.

## Design

Single audit-and-rephrase pass. Sweep the entire repo (case-insensitive `human`) and, for each hit, classify:

1. **Doc prose** — rephrase in terms of "Flagship Player" and "follower players". Where a doc explicitly says "the Frame does not know which player is human vs AI", rephrase to convey the underlying invariant ("the Frame does not privilege any player index — all are AI-driven; one is the flagship that others follow"). Preserve the load-bearing claim (Frame symmetry across players); only the human/AI framing changes.
2. **Code identifiers** — likely none. If any (`bIsHuman`, `kHumanPlayer`, etc.) exist, rename to flagship-based names (`bIsFlagship`, `kFlagshipPlayer`) in the same pass. Search both `human` and `Human` to catch camelCase.
3. **Comments inside source files** — rephrase as docs above.
4. **Asset names / filenames** — out of scope unless one is found; if found, flag in the plan's Notes section rather than auto-rename (filename changes ripple into vcxproj / data pack / save-format references).

False positives to ignore: third-party libraries under `ThirdParty/` (DO NOT modify). Variants of `human` inside license text or library docs are kept as-is.

## Out of scope

- Renaming "Flagship" itself or revisiting the flagship/follower architecture.
- Save-format compatibility (this plan does not change persistent identifiers).
- Network protocol identifiers (none expected to use "human"; if found, flag and defer to a separate plan — protocol changes need a version bump).
- Subdirectory `CLAUDE.md` rewrites that go beyond the human/AI phrasing — keep diffs minimal.

## Acceptance criteria

- Repo-wide case-insensitive grep for `human` returns only `ThirdParty/` matches and unrelated English usage (e.g., a comment about "human-readable JSON") with zero hits referring to a player concept.
- The two known CLAUDE.md hits above are rephrased to use Flagship/follower terminology while preserving the per-file load-bearing technical claim.
- No build break in `BrokenEngineSandbox` client or server projects.

## Critical files

- `Projects/BrokenEngineSandbox/Source/CLAUDE.md` — known hit (Frame symmetry blurb).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/CLAUDE.md` — known hits (collection scope blurb).
- Whole repo — sweep required; additional hits expected in adjacent CLAUDE.md docs (`Frame/`, `Network/`) and possibly inline comments in `Players*.cpp` / `Spaceships.cpp`.

## Notes

- A single-session pass is correct here — this is documentation hygiene, not a code change. Risk is low; the only failure mode is missing a hit, which the acceptance grep catches.
- The user's directive is canonical: "no references" — that's the bar, not "fewer references". Treat this as exhaustive.
- If a hit refers to gameplay direction ("the human flies the lead ship") it should rephrase to flagship; if a hit refers to an operator/developer ("human-readable") it stays as-is.

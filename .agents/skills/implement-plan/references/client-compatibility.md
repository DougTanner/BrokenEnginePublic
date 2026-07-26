# Client Compatibility

Host mapping for the canonical context-isolation default
(`../../../references/subagent-reporting.md`, required by root `AGENTS.md`).
The manager gives the single implementer no inherited conversation context:

- Codex dispatches with `fork_turns: "none"`.
- Claude dispatches with a fresh, self-contained prompt.

The shared brief in `SKILL.md` is authoritative on required context. Do not use
a positive Codex turn fork or depend on prior Claude conversation state.

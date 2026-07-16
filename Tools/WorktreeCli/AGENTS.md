# WorktreeCli

Standalone Windows console application for repository meta-management. It owns serialized MSBuild invocation, landing leases, queue/row claims, and executable plan-order parsing/mutation; it never connects to a game endpoint or manages a harness lock.

- Use `Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe` only from the provisioned primary Output link. Source changes require the explicit AgentTools primary-maintenance workflow.
- `lock token|claim|status|refresh|recover|release|steal` operates only on landing locks identified by `--repo`; do not add `--domain` or harness-key support.
- `plan` and `build` preserve their existing schemas, failure modes, atomicity, and exit codes. `0` is success, `2` is a state conflict/negative result, and `1` is usage, transport, or OS failure.
- Keep source/header membership synchronized between `WorktreeCli.vcxproj` and `.filters`. Shared Windows/coordination code lives in `Tools/ToolCommon` and is compiled by both tool projects.
- Worktree session admission and maintenance use `.agents/scripts/WorktreeCliSessionExclusion.psm1`; bootstrap and provisioning require both WorktreeCli and AgentHarness outputs to be valid.

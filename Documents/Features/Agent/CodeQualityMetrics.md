# Integrate Deterministic Verbosity and Structural-Erosion Analysis

## Summary

- Treat this as Tier 3: it adds a shared ThirdParty dependency and integrates one skill into two independently owned workflows.
- Add [`scb-check`](https://github.com/gabeorlanski/scb-check/tree/f808b0fb214501092ae3c7d914dfe98fe2a312af) as `ThirdParty/scb-check`, pinned to peeled `v0.2.0` commit `f808b0fb214501092ae3c7d914dfe98fe2a312af`.
- Add `$code-quality-metrics` for deterministic C++ snapshots and baseline/current comparisons. History graphs and plotting remain out of scope.
- Keep upstream pristine. Broken Engine’s `.h` support, reporting, comparison, and remediation logic live entirely inside the skill.
- No C++, GLSL, Prebuilts, Visual Studio project, or upstream-source changes are planned.

## Scope contract

The following scope is both target and ceiling. Implement the smallest complete change described here; add no abstractions, configuration, refactors, cleanup, or fixes to adjacent code encountered during implementation.

### In scope

- `.gitmodules` — add only the `ThirdParty/scb-check` submodule stanza.
- `ThirdParty/scb-check` — add only the gitlink pinned to the approved upstream commit; never edit upstream files.
- `ThirdParty/AGENTS.md` — change only the Boundaries guidance needed to distinguish prohibited repository-owned instruction files from preserved upstream-owned instruction-named files in pristine submodules.
- `.agents/skills/code-quality-metrics/**` — add the new skill package described below: `SKILL.md`, Codex sidecar, PowerShell entrypoint, Python analyzer, and focused metric/remediation references.
- `.agents/skills/external-deep-analysis/SKILL.md` — change only the workflow and summary regions needed to add bounded Phase 0 metric hints and their failure/reporting contract.
- `.agents/skills/repo-code-review/SKILL.md` — change only Required Inputs, Workflow, applicable duplication guidance, and Output regions needed for fixed-baseline Compare evidence and its advisory boundary.
- `Documents/FreshMachineSetup.md` — change only prerequisite, clone/submodule, and existing-checkout recovery guidance for Python 3.12 and `ThirdParty/scb-check` hydration.
- `README.md` — change only the corresponding setup/prerequisite summary and link text.

### Out of scope

- History analysis, graphs, plotting dependencies, or importing the earlier history prototype wholesale.
- Any C++, GLSL, Prebuilts, Visual Studio project/filter, DataPacker, provisioner, WorktreeCli, AgentHarness, root policy, scheduler, or upstream-source implementation change.
- Automatic source remediation, score-based quality gates, changes to Debt Score, or treating a metric regression as a correctness finding.
- Supporting additional languages, metrics, profiles, configurable thresholds, CI integration, or generalized plugin/framework infrastructure.

## Dependency and Skill Contract

- Register the gitlink in `.gitmodules`. Preserve upstream’s Apache-2.0 [`LICENSE`](https://github.com/gabeorlanski/scb-check/blob/f808b0fb214501092ae3c7d914dfe98fe2a312af/LICENSE), while documenting that its [`pyproject.toml`](https://github.com/gabeorlanski/scb-check/blob/f808b0fb214501092ae3c7d914dfe98fe2a312af/pyproject.toml) declares MIT. Both are permitted; make no synthesized license claim and let DataPacker copy the exact LICENSE.
- Clarify `ThirdParty/AGENTS.md`: repository agents must not add or edit instruction files inside external trees; upstream-owned `AGENTS.md`/`CLAUDE.md` files in a pinned pristine submodule are preserved as upstream data and do not override Broken Engine policy.
- Add `.agents/skills/code-quality-metrics/` with `SKILL.md`, `agents/openai.yaml`, a PowerShell entrypoint, Python analyzer, metric contract, and remediation reference. Claude chaining remains enabled; Codex implicit selection is disabled while manual calls and explicit parent chaining remain available.
- Public entrypoint:

  `Invoke-CodeQualityMetrics.ps1 -RepositoryRoot <absolute> -Mode Snapshot|Compare (-Target <relative-path> -Scope Exact|Directory|Recursive | -Targets <json>) [-Baseline <full-sha>] [-Digest] [-Phase0Hints] [-OutputPath <path>]`

  `Compare` requires `Baseline`. Reviews pass a `broken-engine-code-quality-targets/v1` file listing sorted unique paths (`{schemaVersion,paths}`); the analyzer derives same-path, cross-path rename, and one-sided add/delete pairs itself, and a listed path present in neither corpus fails. `-Digest` emits the `broken-engine-code-quality-evidence/v2` summary built in-process from the report, and `-Phase0Hints` implies it and adds the bounded investigation hints. Logs go to stderr; `OutputPath`, when requested, always receives the full `broken-engine-code-quality-metrics/v2` report, while stdout receives that same report normally or the `broken-engine-code-quality-evidence/v2` digest when `-Digest` or `-Phase0Hints` is set. Exit `0` means analysis completed; exit `2` means invalid input, bootstrap failure, capture drift, or analysis failure, and forwards the analyzer's stderr verbatim.

- Emit `broken-engine-code-quality-metrics/v2` JSON with tool/runtime identity, corpus and target manifests, coverage, parse failures, metrics, file/area outliers, clone groups, high-complexity functions, and comparison evidence. Its `tool` object is `{adapterVersion,lockSha256,python,disableSg}` with `adapterVersion` set to `"5"`. Use relative POSIX paths, sorted collections, UTF-8/LF, rounded 12-decimal floating values, and no timestamps, durations, absolute paths, or other volatile fields.
- Bootstrap an ignored venv under `Temp/CodeQualityMetrics/`, keyed by exact Python interpreter identity and `requirements.lock` hash. Stage the analyzer source from the running checkout's own `ThirdParty/scb-check` — resolving provisioned links to their targets and copying `src` plus `requirements.lock` while excluding `__pycache__` — into a fresh ignored stage, validate it, and import that staged source; the stage is only an import source, not a metric, cache, or report identity. Install upstream dependencies with `pip --require-hashes --only-binary=:all:`; these PyPI artifacts must match hashes committed in the trusted GitHub lock.
- Make bootstrap concurrency-safe with a per-key named mutex, unique staging venv, import/`sg.exe`/identity validation, completion marker, and atomic same-volume promotion. Rebuild only invalid caches beneath a validated Temp root.
- Require Python 3.12+ and document first-run network behavior and existing-checkout submodule hydration in README/FreshMachineSetup.

## Metric and Workflow Behavior

- Analyze repository-owned C++ discovered from tracked plus untracked/nonignored files, excluding `ThirdParty`, `.agents`, `.claude`, `Temp`, and ignored/generated files.
- The single analysis behavior, labelled `BrokenEngineExtended` in every report, adds `.h` to upstream’s C++ dispatch through a process-local adapter, but excludes pure GLSL `.h` files beneath contiguous `Data/Shaders` components. It retains both `ShaderLayouts.h` and `ShaderLayoutsBase.h` as dual-language C++ inputs, selecting their direct `BT_ENGINE` branch in a fixed byte-preserving normalized parser capture. That capture masks only code tokens for the locked parser gaps: compatibility/calling-convention/SAL tokens, namespace template declarations, deleted declarations, nested braced aggregate designators, assignment-comma folds, and empty-brace function/template defaults; it preserves raw manifests, compiler input, source-span hashes, coordinates, and all CR/LF bytes. Exact pure-GLSL targets fail with `target is not classified as C++ for BrokenEngineExtended: <path>`. There is no second profile and no profile selection.
- Compute:

  - Function mass = `cyclomatic complexity × sqrt(function SLOC)`.
  - Structural erosion = high-complexity mass (`CC > 10`) divided by total mass.
  - C++ verbosity = the union of clone-flagged SLOC divided by total SLOC; at this pin, non-Python verbosity is clone-only.
  - Higher values are worse. Zero denominators produce `applicable: false` with a null value and preserved numerator/denominator.

- Analyze the complete owned corpus once so target files can detect clones outside their directory. Report aggregate weighted corpus/target metrics, coverage, exact clone instances, high-CC functions, absolute contributors, and the ten largest positive file/area outliers relative to the corpus average.
- Use the prototype’s Broken Engine area grouping: Engine and project Source subsystems, Common/Tools children, and top-level fallback.
- Compare mode archives the fixed baseline and separately captures current tracked plus untracked/nonignored source into an immutable temporary tree. Record path, mode, and SHA-256 identities before and after capture; exit `2` on capture-manifest or content drift.
- Report whole-corpus, target, and common-parsed-cohort deltas; numerator/denominator changes; introduced/resolved clone groups and instances; unchanged clone counterparts; coverage changes; and function changes.
- Match functions within baseline/current file pairs by undecorated owner, name, and signature only when unique on both sides. Report overloads or duplicate identities as ambiguous/unmatched without directional claims. Coverage/identity changes suppress total-score “improved/regressed” labels while retaining raw and common-cohort evidence.
- Add mandatory Phase 0 to `$external-deep-analysis`: run corpus-plus-target Snapshot, then pass bounded target outliers, clones, and high-CC functions into architecture/refactor phases as investigation hints. They cannot expand scope, create findings or Plans, or alter Debt Score. Operational failure blocks the pipeline; parse skips remain explicit residuals.
- Add Compare to `$repo-code-review` using its fixed baseline and its authorized changed-file target list. Regression alone never changes PASS or becomes a finding. Only independent source inspection can promote a substantial new near-copy through the existing duplication rule or prove another existing correctness violation. Structural-erosion changes remain advisory/follow-up evidence. Operational failure makes the review incomplete, not a correctness finding.

## Remediation Guidance

- Record that the paper’s anti-slop prompts lower initial scores in Python experiments but do not stop degradation slopes; it does not provide a validated post-hoc C++ remediation algorithm. [Paper v1](https://arxiv.org/html/2603.24755v1)
- For clone evidence, inspect semantics and ownership, then prefer deleting dead duplication, reusing an existing helper, or extracting/parameterizing genuinely common logic while preserving deliberate client/server and collection mirrors.
- For erosion evidence, identify cohesive responsibilities or branch families and apply minimal behavior-preserving decomposition or an existing dispatch pattern.
- Warn against trivial wrappers, helper proliferation, removing valid checks, denominator gaming, or improving one metric by worsening the other.
- Cite related primary work as supporting context—not proof of lowering Broken Engine’s metrics: [Microsoft RASE](https://www.microsoft.com/en-us/research/publication/does-automated-refactoring-obviate-systematic-editing/), [duplicate-aware refactoring](https://arxiv.org/abs/2502.04073), and [SBSRE method decomposition](https://arxiv.org/abs/2305.03428). Every recommendation still requires source inspection, normal verification, and remeasurement.

## Verification and Delivery

- Implement in disjoint slices: dependency/policy/setup docs; new metric skill; deep-analysis/review integration.
- Run scenario checks without adding unit tests:

  - Exact URL, gitlink, tag peel, pristine status, license inventory, and lock hash.
  - Clean, incomplete-cache, and concurrent-first-run bootstraps.
  - Two identical snapshots produce byte-identical JSON; emitted components independently recompute both scores.
  - C++ dispatch includes `.h`; an exact pure-GLSL `.h` target is rejected.
  - Exact, nonrecursive, recursive, added/deleted/renamed, zero-denominator, parse-failure, and current-capture-drift cases.
  - `HEAD` versus unchanged checkout yields zero comparable deltas and no clone/function changes.
  - Line insertion before overloaded functions leaves unique matches stable and reports ambiguous overloads conservatively.
  - Synthetic clone and high-CC changes produce the expected advisory evidence and unchanged correctness boundary.

- Before landing, run the unmodified provisioner in a disposable scratch primary/linked-worktree repository containing the exact new `.gitmodules` entry/gitlink, hydrated source, and nonempty dummy shared-output prerequisites; verify the link farm and pin.
- Run fresh coherence review, `$validate-skill` for all three changed skill packages, token measurements, Tier-3 adversarial review, and final read-only `$verify-changes`. No MSBuild is scheduled because no compiled/project bytes change.
- After required landing sign-off, `$finalize-changes` must hydrate the real primary with `git submodule update --init --recursive -- ThirdParty/scb-check`, run the actual provisioner, and run the existing DataPacker attribution path, verifying the emitted `scb-check/LICENSE` matches upstream exactly. Do not release completion/claim state until these post-landing checks pass.
- Fixed implementation baseline: `dd23b29b2f54def13e14e5b876449d6be8d383e4`; re-run preparation if the relevant tree changes before implementation.

# Remediation

Treat clone and complexity metrics as navigation, not verdicts. [Paper v1](https://arxiv.org/html/2603.24755v1)'s
anti-slop prompts lower initial scores in Python experiments but do not stop degradation slopes, and
the paper provides no validated post-hoc C++ remediation algorithm. [Microsoft RASE](https://www.microsoft.com/en-us/research/publication/does-automated-refactoring-obviate-systematic-editing/),
[duplicate-aware refactoring](https://arxiv.org/abs/2502.04073), and [SBSRE method decomposition](https://arxiv.org/abs/2305.03428)
are supporting context, not proof that an intervention lowers Broken Engine's metrics.

Inspect the concrete clone interval, semantics, ownership, and callers first. Prefer deleting dead
duplication, reusing an existing helper, or extracting/parameterizing genuinely common logic. Preserve
deliberate client/server and collection mirrors when their contracts differ. Do not game the metric
with trivial wrappers, helper proliferation, removing valid checks, denominator changes, or an
improvement to one metric that worsens the other.

For erosion evidence, identify a cohesive responsibility or branch family, then apply the smallest
behavior-preserving decomposition or existing dispatch pattern. Every recommendation still requires
source inspection, normal verification, and remeasurement. Keep metrics advisory: never automatically
remediate, score contributors, or create a quality gate from them.

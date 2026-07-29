# Metric Contract

## Public input

`Invoke-CodeQualityMetrics.ps1` accepts exactly one `-Mode Snapshot|Compare`. Snapshot requires `-Mode Snapshot -Target
<relative-POSIX-path> -Scope Exact|Directory|Recursive`. Compare requires `-Mode Compare
-TargetManifest <UTF-8 JSON file> -Baseline <full Git SHA>`. Both accept an absolute
`-RepositoryRoot`, `-Profile BrokenEngineExtended|StrictUpstream`, and optional `-OutputPath`.

The target manifest is UTF-8 JSON:

```json
{"schemaVersion":"broken-engine-code-quality-target-manifest/v1","pairs":[{"baseline":{"mode":"100644","path":"Engine/Source/A.cpp","sha256":"..."},"current":{"mode":"100644","path":"Engine/Source/A.cpp","sha256":"..."}}]}
```

`baseline` and `current` are nullable identities, with at least one side required. Pairs are ordinal-sorted by baseline path then
current path. Each non-null identity contains exactly `path`, `mode`, and `sha256`; paths are
canonical relative POSIX paths, modes are ordinary Git blob modes, and hashes are lowercase SHA-256.
The analyzer verifies every listed raw identity, pair truth, casing, profile support, and ordinary
non-reparse file before capture. A same-path two-sided pair is valid; a one-sided pair must be an
actual addition or deletion. A two-sided cross-path pair is valid only when its baseline path is
absent from the current corpus, its current path is absent from the baseline corpus, and Git's
deterministic `--no-index --no-ext-diff --find-renames=50%` similarity detection reports a rename
for the immutable baseline and current bytes. It rejects unknown, malformed, unrelated, and
still-existing cross-path pairs.

## Output

Output is compact UTF-8 JSON without BOM and with exactly one final LF. Object keys retain the
construction order specified below; arrays whose members are sets use the stated ordinal sort.
Finite floats round to 12 fractional digits and negative zero is zero. No absolute or volatile
value appears. Top-level fields, in this order, are `schemaVersion`, `mode`, `profile`, `tool`,
`targetSelection`, `baseline`, `current`, and `comparison`; `schemaVersion` is
`broken-engine-code-quality-metrics/v2`.

`tool` is `{adapterVersion,lockSha256,python,disableSg}` with `adapterVersion` set to `"3"`; its `python` value is
`{implementation,version,architecture,executableSha256}`. `targetSelection` is
`{kind,scope,target,paths}` for Snapshot and `{kind,pairs,paths}` for Compare. Compare canonicalizes
each selected pair to `{baseline,current}` and each identity to `{path,mode,sha256}`, independent
of input JSON member order. Context-change rows are `{path,change}`.

`baseline` and `current` are CaptureViews. A CaptureView contains `corpusManifest`,
`targetManifest`, `corpusCounts`, `targetCounts`, `skips`, `corpusMetrics`, `targetMetrics`,
`files`, `areas`, `outliers`, `targetOutliers`, `cloneGroups`, and `highComplexityFunctions`. A MetricValue is exactly
`{applicable,value,numerator,denominator}`. Skips use `{path,code:"upstream-omitted"}`. Files and
areas use `{path,area,metrics}` and `{area,metrics}` respectively. Counts use
`{supported,parsed,omitted}`. `outliers` are corpus-wide positive top-ten per metric. `targetOutliers`
are target-only positive top-ten file and area buckets, calculated from the complete parsed target
cohort against the corresponding corpus metric; neither bucket filters the already-truncated
corpus-wide `outliers`. Both use `items`, `totalCount`, and `truncatedCount`, ordered
delta-descending then key.
Areas are `Engine/Source/<child>`, `Projects/<project>/Source/<child>`, `Common/<child>`,
`Tools/<child>`, a direct parent label, another nested first component, or `[root]`.

Clone instances are complete, deduplicated by `(groupHash,path,startLine,endLine)`, and contain
`groupHash,path,startLine,endLine,sloc,sourceSha256,occurrenceOrdinal`. Their source hash is the
exact LF-normalized inclusive source span. `verbosity` is the union of every emitted CloneBlock's
own inclusive interval intersected with parsed SLOC, divided by parsed SLOC. `structuralErosion` is mass of
functions with CC above ten divided by total mass; mass is `CC * sqrt(SLOC)`.

`comparison` contains `contextChanges`, `corpus`, `target`, `commonParsedCohort`, `coverage`,
`cloneGroups`, `cloneInstances`, and `functions`. Delta families use
`{baseline,current,valueDelta,numeratorDelta,denominatorDelta,classification,suppressionReasons}`.
`coverage` uses `{baseline,current,deltas}` and each coverage count follows the CaptureView count
order. Clone-group rows use `{groupHash,change}`; clone-instance rows use
`{key,baseline,current}` with key `{groupHash,sourceSha256,sloc}`. Function rows use
`{identity,ambiguous,baseline,current}` when ambiguous and append `deltaCc` when directional.
Function identity is nullable owner/name/signature. Before identity comparison, the analyzer removes
only the exact current source filename module prefix plus `.` from a non-null owner; nested and
class owners remain intact. The signature is parser-native C++
declaration tokens with parameter binding names and defaults omitted; an unavailable or duplicate
signature is null. Directional function comparison requires one non-null identity on each side and
no overloads or duplicate identities for that owner/name; ambiguous or unmatched functions have no
directional claim. Function comparisons include crossings where either CC exceeds ten. Clone matching uses group hash,
source hash, and SLOC, allowing an authorized rename; duplicates order by start line. The comparison
selects only clone group hashes touched by target-side instances. It classifies each selected group
from complete baseline and current corpus membership, emits target-pair instances, then emits each
unchanged external counterpart with the same group/key/path/inclusive span on both sides. All target
paths are excluded from external counterparts; unrelated groups and duplicate records are omitted.
Clone-instance rows sort by group hash, source hash, SLOC, path, inclusive span, then side presence.
Suppression
reasons are only `baseline-inapplicable`, `context-change`, `current-inapplicable`,
`membership-change`, or `parse-status-change`, sorted ordinally.

`commonParsedCohort` contains `baselinePaths` and `currentPaths`: paired, two-sided target
identities that parsed successfully on their respective sides, so an authorized rename participates
without pretending its paths are equal. Its metrics suppress only for applicability and context
changes. Corpus and target deltas additionally suppress for reachable logical membership and paired
parse-status changes; a content-only modification remains comparable.

## Analysis capture

Identity validation, manifests, capture-drift checks, and emitted `sourceSha256` values always use
raw source bytes. `BrokenEngineExtended` then creates a parser-only, byte-preserving capture: it
replaces masked non-newline bytes with ASCII spaces, retaining every byte count, CR/LF byte, physical
line, and byte column. `StrictUpstream` writes raw bytes unchanged.

The fixed normalizer lexically skips ordinary strings, character literals, raw strings, line comments,
and block comments before recognition. It masks only identifier-bounded `__restrict` and
`XM_CALLCONV`; a preprocessor directive whose `#` is the first non-horizontal-whitespace code byte of
a physical line, including backslash-continuations and trailing comments; and a leading `template
class` or `template struct` explicit instantiation that ends with `;` on its logical line and has no
body braces. It does not alter real C++ files, compiler input, profile support, coverage policy, or
bootstrap identity. Parser-derived group hashes may reflect this capture, while typed signatures,
function/clone coordinates, SLOC, and raw-span hashes retain source coordinates and raw identities.

The entrypoint authenticates the provisioned source internally, archives it into a fresh ignored
stage, and imports the staged source. Its gitlink pin is only an archive selector. The venv key and
`complete.json` remain keyed only by Python interpreter identity and `requirements.lock` hash; they
do not include analyzer output, normalizer, or gitlink identity.

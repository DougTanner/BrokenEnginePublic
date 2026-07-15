# Scope routing fixtures

Verification-only dry-run fixtures for `/next-plan`'s scope-authority contract. These are not engine tests and have no executable PowerShell oracle. A verifier applies the current skill text to each exact input and checks the expected agent count, candidate routing, execution membership, and provenance.

## Contents

- [Common rules](#common-rules)
- [F1 — no-trigger narrow plan](#f1--no-trigger-narrow-plan)
- [F2 — exact mirrored required propagation](#f2--exact-mirrored-required-propagation)
- [F3 — historical server-save result propagation](#f3--historical-server-save-result-propagation)
- [F4 — replay-start H001 is a delta](#f4--replay-start-h001-is-a-delta)
- [F5 — replay-stop H002 is a delta](#f5--replay-stop-h002-is-a-delta)
- [Contract-negative expectations](#contract-negative-expectations)

## Common rules

- `agent count` means Step 6 sibling-sweep plus extension-review agents only; Step 0, Step 4, and plan-audit agents are outside this count.
- A triggered case uses one fresh Sonnet code-search agent under `be-agent-report/v1`; no extension-review agent exists.
- Incoming report IDs such as `H001` are search-report evidence IDs. The main assigns stable canonical `C###` IDs and preserves the H ID in evidence.
- Every case begins with a verified immutable source packet. `S###` excerpts below are its exact source-step inputs.
- A tracked author baseline is the most recent commit at or before the fixed baseline that changed the plan path to the captured blob, not a later commit whose tree merely retained matching bytes. Untracked, mismatched, or uncertain path/blob identity makes the author baseline unavailable and triggers a sweep.

## F1 — no-trigger narrow plan

Input source packet:

```text
Tracked source: <matching author commit>:Documents/Plans/Agent/Narrow.md@<matching blob>
S001: In `.agents/skills/example/SKILL.md`, replace the private heading `Old label` with `New label`; no behavior or references change.
```

Repository facts: the heading is private to that file; no exhaustive/mirrored claim, public/cross-TU signature, shared/serialized/CRC/collection member, incomplete search, or relevant post-authoring drift exists.

Expected:

- Context: `Sibling sweep: not triggered — private one-file text replacement has no trigger; author baseline <commit>; evaluated Old label in .agents/skills/example/SKILL.md`.
- Agent count: `0`.
- Candidates: none.
- Execution: `[S001]` only.
- Provenance: `S001` only; approved-delta ledger `none`.

## F2 — exact mirrored required propagation

Input source packet:

```text
Tracked source: <matching author commit>:Documents/Plans/Agent/Mirrored.md@<matching blob>
S001: Rename `AcquireSession` to `RegisterSession` in the mirrored wrapper command family and update its callers without changing parameters, behavior, build mode, or verification.
```

Repository facts: the source names a mirrored family. The source cites `Scripts/Start.ps1`; the sole search report finding is `H001`, `Scripts/Resume.ps1`, where the token-identical call must use the same new name. Both scripts have the same contract and verification class.

Expected:

- Context: `Sibling sweep: triggered — captured source claims a mirrored wrapper command family`.
- Agent count: `1` Sonnet.
- Candidate: `C001`, evidence `H001`, Identical/high, state `Folded required propagation P001 derived from S001 and C001`.
- Execution: `[S001]` and `[P001]`; `P001` states the exact one-token caller rename in `Scripts/Resume.ps1`.
- Provenance: S/P entries agree; approved-delta ledger `none`.

## F3 — historical server-save result propagation

Historical anchors: source-plan blob `a628c5314b63338e5bbb50159efff0f8253c4e06` at baseline `98b5272c0cb836b822cc5692484107768426b103` (`Documents/Plans/Save/ServerSaveFailureReporting.md`), implemented by `90f7ed3450b9318d89b3de74cb7e1271f12789ce`.

Input source packet for the narrow propagation exercise:

```text
S001: Make the agent `save` command report failure when `GameSaveLoad::WriteGrid`'s atomic write fails by returning the captured `bWritten`; preserve save format, server affinity, and existing success behavior.
```

Repository facts: `WriteGrid` feeds the two `ServerSave` overloads, which feed `CommandSave`; the result cannot reach the requested agent boundary without changing those exact return/consume sites. The signature change triggers a sweep. Search evidence is `H001` for `ServerSave` and `H002` for `CommandSave`.

Expected:

- Agent count: `1` Sonnet.
- `C001/H001` becomes `P001 derived from S001 and C001`: both `ServerSave` overloads return/forward the same bool.
- `C002/H002` becomes `P002 derived from S001 and C002`: `CommandSave` consumes false as its existing error-envelope convention and emits success only after true.
- Both propagations are necessary intermediate result handling that realizes the exact agent-visible failure behavior already authorized by S001; neither adds a dimension beyond S001.
- Execution: `[S001]`, `[P001]`, `[P002]`. No replay lifecycle site is executable.
- Provenance: exact S/P chain; approved-delta ledger `none`.

## F4 — replay-start H001 is a delta

Input source packet:

```text
S001: Thread `WriteGrid`'s write result through the requested server-save path to the agent `save` response. Existing void-context callers, including the F7 replay-grid write, discard the return.
```

Incoming sweep finding:

```text
H001: At `GameSaveLoad::SyncReplayTick` replay start, consume `WriteGrid(false)`, log an error, and abort recording startup.
```

Expected:

- Trigger: public/cross-TU signature; agent count `1` Sonnet.
- Canonical candidate: `C001` preserves evidence `H001`, Related/high, state `Delta requested`.
- Reason: aborting replay startup adds visible behavior, a replay persistence lifecycle/failure mode, and a different verification class; the source explicitly says this caller discards the return.
- Execution/provenance: no P/D entry and no replay-start step. Final presentation is forbidden until the grill changes C001 to approved D, follow-up, or rejected.

## F5 — replay-stop H002 is a delta

Input source packet: identical to F4.

Incoming sweep finding:

```text
H002: At `GameSaveLoad::SyncReplayTick` replay stop, aggregate manifest, coordinate-writer, and metadata results; clear writers; log one complete-replay failure.
```

Expected:

- Trigger: public/cross-TU signature; agent count `1` Sonnet.
- Canonical candidate: `C001` preserves evidence `H002`, Related/high, state `Delta requested`.
- Reason: multi-component replay commit/cleanup semantics add a subsystem lifecycle, failure aggregation, persisted-set invariant, and verification obligations absent from server-save result propagation.
- Execution/provenance: no P/D entry and no replay-stop step until an exact user approval creates `D001 derived from C001`.

## Contract-negative expectations

| Perturbation | `/plan-audit` expectation | `/implement-plan` expectation |
|---|---|---|
| Source packet omitted when the canonical plan has the authority sections | Finding: missing independent source evidence | Stop before editing: the exact source-packet identity and successful audit result are required; do not infer provenance |
| Packet bytes do not match recorded SHA-256 | Finding: hash mismatch; synthesized S IDs are untrusted | Stop before editing: failed or mismatched packet/audit evidence grants no authority |
| Execution step has no matching S/P/D entry, or a P/D mapping names the wrong candidate | Finding: untraceable or mismatched execution | Stop before editing |
| `Delta requested C001` remains | Record it as a required grill decision and ensure it is absent from execution | Stop before editing |
| Grill chooses `Follow-up pending C001` | Keep it out of execution and create indexed residual `R001 -> C001` | Does not block base implementation; before cleanup, `/create-follow-up-plans` must map C001 to a created follow-up plan |
| Ledger contains `D001` but supplied approved-delta summary is `none`, omits it, or differs | Finding: approved-delta evidence mismatch | Stop before editing |
| Successful audit identifies D IDs/ledger/summary as `none`, but the final plan contains `D001` | Fresh audit required on the exact final D authority state | Stop before editing: the supplied audit result does not cover the final D state |
| Ledger is `none` and supplied summary is `none` | Pass this consistency check | Continue if all other preflight checks pass |

If the base plan is rejected or deferred, do not claim that a follow-up was created: retain every `Follow-up pending C###` in the canonical plan. On successful execution, Step 8 cleanup is blocked until the `/create-follow-up-plans` report maps each pending candidate ID to its created plan path/ID.

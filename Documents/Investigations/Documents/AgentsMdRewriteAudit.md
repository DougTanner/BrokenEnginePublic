# AGENTS.md Rewrite Audit (commit `280e89b7`)

Status: findings record. It lives in `Documents/Investigations/` because it reports what an audit
found and proposes fixes, rather than committing to a decision-complete implementation, so it is
never a scheduler input and carries no `broken-engine-plan/v1` metadata marker. Individual sections
of it can become Plans once the owner decides which restores to make; a Plan would move to
`Documents/Plans/<area>/` with byte-zero metadata.

## 1. Purpose and standing

On 2026-07-21 a single commit rewrote nearly every `AGENTS.md` directory-memory document in this
repository and cut the corpus roughly in half. This document records a forensic audit of that
rewrite: what durable engineering knowledge the rewrite removed, what it removed correctly, what it
asserted that is not true, and what in the process allowed it to happen.

Standing and scope:

- **No `AGENTS.md` file was modified by the audit.** The audit was read-only. Every "recommended
  text" below is a proposal awaiting the repository owner's decision, not an applied change.
- This is a findings record, not an executable plan. Nothing here is scheduled, claimed, or
  automatically actioned.
- The audit judged the documents against the source tree at the `HEAD` that existed when it ran
  (2026-07-31). The repository keeps moving. Section 12 explains how to re-check a finding before
  acting on it.
- **Section 10 is a precondition on acting for every finding in this report, without exception.**
  Four commits improved these documents after the rewrite, and no restore may be made in a way that
  undoes them. Read section 10 before editing any file on the strength of any P1, P2, or P3 here.
- The audit produced 12 working reports under `Temp/AgentsMdAudit/`. `Temp/` is not tracked by Git,
  so those files will not survive. **This document is the surviving record**: every P1 is carried
  here in full, and every P2 with enough detail to act on without the originals.

## 2. What happened

### The commit

The change is commit `280e89b7`, "Implement plans", landed 2026-07-21. It rewrote **79 `AGENTS.md`
files**: 71 existing files edited, 8 newly created.

It was authored on 2026-07-20 by a GPT-5.6 "Sol" Codex session (parent thread
`019f7f68-8928-7fd2-82e9-74c304d5aa39`, about 34 subagents, working in the Codex worktree
`c0873316-6f45-4f96-8312-9d5104615a4d`). The session ran about two hours and took six user turns.

### The originating prompt, verbatim

> "Plan a full audit of all AGENTS.md in the entire repo (excluding the root one). Using the lens of
> $update-claude-docs 1. Verify the information in the AGENTS.md is accurate 2. Trim out any noise
> like update logs 3. Make sure no important information is missing 4. Keep it concise, especially
> if they are over the token budget"

Two later user messages refined scope: "aggressively use subagents … keep main context clean" and a
single correction — "1. Include Managers/*.AGENTS.md 2. ThirdParty already has a AGENTS.md, do not
create new ones inside the exteranl submodule folders". The remaining three user turns were
approvals: "Implement the plan.", "approve recommended matrix", "Confirm landing".

### How much was removed

Measured directly from the Git blobs:

| Measure | 71 rewritten files | Including the 8 created files |
|---|---|---|
| Bytes | 386,120 → 157,950 (**−59%**) | 386,120 → 175,542 (**−55%**) |
| Words | −59% | −54% |

The session's own independent `bt-token-v1` measurement over the 82 documents it audited agrees:
104,853 → 47,794 tokens, **−54%**.

**Line count is misleading here and should not be quoted.** By line count the change looks like only
−17%, because long prose paragraphs were replaced by short bullet lines — the same information
density collapse shows up as a modest line reduction and a very large byte reduction. Bytes, words,
and tokens all agree on roughly −55%.

### The state today

Almost nothing has been restored since. Today's files still match the post-rewrite state nearly
everywhere; the exceptions are noted per finding, and where a later commit changed a document the
audit attributes the change to that commit rather than to the rewrite.

### What in `280e89b7` was not this rewrite

`280e89b7` was not a documentation-only commit. It was a squash of several parallel sessions from
that day, and it also landed the `ClientSessionBase` → `ClientSessionRuntime` refactor and other
work. Consequences for reading this report:

- Some apparent documentation deletion in the network tree was legitimate relocation: text
  describing `ClientSessionBase` was removed because that class ceased to exist in the same commit.
- The root `AGENTS.md` changes inside `280e89b7` came from other sessions squashed into the same
  landing, not from this task. (One separate wrinkle is recorded at P3 in section 7: the rewrite
  session itself did make three small root edits despite the prompt excluding the root document.)

### Size was almost never the constraint

The repeated pattern across every group is that files already inside their token target were cut
hard anyway:

- All eight `Engine/Source/Graphics/Managers/*.AGENTS.md` linked reference documents were inside the
  2,000-token leaf target before the cut (748–1,195 tokens) and were still cut 55–75%.
- `Common/AGENTS.md` was about 600 tokens over a 4,000 target; roughly 1,800 tokens of content were
  removed to fix a 600-token overage.
- The DataPacker `ExportJobs` hub was 3,288 tokens against a 4,000 target and was cut anyway.
- `Engine/Source/File/AGENTS.md` was 37 tokens over a 2,000 target. 1,366 tokens were removed — a
  37× overshoot — on an audit note that read "budget only… No missing durable rule found".

Every one of the eleven file auditors independently reported that **no recommended restore in this
document is blocked by a token budget.** Current sizes leave ample headroom everywhere.

## 3. How the audit was run

Twelve auditors ran in parallel:

- **Eleven file auditors**, partitioned by subsystem: G01 Common, G02 DataPacker, G03 EngineCore,
  G04 EngineFrame, G05 EngineGraphics, G06 GraphicsManagers, G07 Network, G08 Shaders, G09
  SandboxCore, G10 SandboxFrameUi, G11 MetaDocsTools.
- **One transcript auditor** (T1), which reconstructed the original 2026-07-20 session from its
  Codex rollout logs and assessed the process.

Method for each file auditor, per the shared brief:

1. Read what the commit did (`git show 280e89b7 -- <path>`) and the full pre-rewrite text
   (`git show 280e89b7~1:<path>`).
2. Check for later drift (`git log --oneline 280e89b7..HEAD -- <path>`).
3. Read the current file on disk and the actual source in that directory (`.h`, `.cpp`, `.glsl`,
   `.ps1`, `.cs`), and verify every claim that remains **and** every claim that was removed.
4. Report every finding with `file:line` code evidence. A finding without evidence was to be dropped
   or explicitly marked as a hypothesis at P3.

Two judgement rules mattered:

- **Judgement was against `HEAD`, not the 2026-07-21 tree.** A deletion of something that is now
  obsolete is a good change. A deletion of something still true and still decision-changing is a
  finding.
- **Later-commit drift was to be distinguished from rewrite damage.** `280e89b7` is roughly 57
  commits behind `HEAD`, and later commits edited some of these documents. Findings caused by later
  commits are labelled as such throughout, so the rewrite is not blamed for them.

### Severity scale (reproduce this to grade consistently)

- **P1** — a durable invariant or safety-critical constraint was lost, or current text is wrong in a
  way that would cause a bad code change. Restore or fix required.
- **P2** — real information loss or inaccuracy that would slow or mislead a future editor, but would
  not by itself cause an incorrect change.
- **P3** — style, wording, or minor imprecision. Optional.
- **OK** — the change was neutral or an improvement.

### What counts as a bad change

1. **Lost invariant** — a rule, ordering constraint, lifetime or ownership contract, determinism or
   allocation constraint, numeric or coordinate assumption, or "why this design exists" rationale
   was deleted and is still true. Most important category.
2. **Lost specificity** — a precise, checkable statement (a named symbol that owns an invariant, a
   concrete threshold, an exact ordering) flattened into prose too vague to act on.
3. **Introduced inaccuracy** — new or condensed text that does not match the code.
4. **Over-compression** — a paragraph compressed until a reader cannot understand the subsystem.
5. **Broken reference** — a path, link, or cited symbol that no longer resolves.

### What counts as a good change (do not revert)

Removal of member, enum, or file inventories; method call-chain narration; changelog or migration
narration; duplicated parent or sibling rules; restating what a declaration already shows; all-caps
emphasis; and merely decorative prose. The `/update-claude-docs` standard explicitly wants these
gone, and the audit consistently declined to file findings on them.

### The standard being audited against

`/update-claude-docs`: state how the code works now; architecture rather than inventories; link to
the authoritative owner instead of duplicating; "remove a sentence unless its absence would plausibly
cause a worse future decision" (which cuts both ways); use established repository vocabulary; leaf
documents target ≤ 2,000 and cross-cutting hubs ≤ 4,000 `bt-token-v1`; size is a target, not a
licence to delete an invariant; do not silently override a parent or sibling invariant; a source
exemplar may replace procedural prose only if the invariant and its reason stay in the document.

## 4. Verdict

**Both halves of this are true and have to be held together.**

Much of the trimming was correct, and the standard did ask for it. The rewrite removed large amounts
of genuine noise: member and enum inventories, per-file listings, method call-chain narration,
changelog fragments, duplicated parent rules, and prose restating what a declaration already showed.
Section 8 records the specific things it got right, including one rewritten invariant that is
strictly more accurate than the text it replaced. Reverting the rewrite wholesale would be a
mistake.

The failure is narrower and worse than "the model went off the rails". The process **had no check
for missing information**, so it overshot with no signal that anything was wrong. The transcript
report supports a specific conclusion:

> The pipeline was instrumented to detect *wrong* text and blind to *absent* text.

Every gate in the two-hour session — token budgets, link resolution, import-stub purity,
`git diff --check`, an absolutes-grep over the new text, a history-words grep, a scope-count check —
either ignores deletion or actively rewards it. The conciseness score (15 of 100) plus a monotonic
budget check made over-compression register as the **strongest possible pass**: the mechanical
verifier reported "Budgets | Pass | Hub max 1,387; leaf max 1,849; zero failures" against ceilings of
4,000 and 2,000. Cutting a hub to 35% of its allowance looked identical to a well-judged trim.

Two implementers explicitly asked a reviewer to check for over-compression. The reviewer named both
areas in its coverage list and returned zero findings on either, with no command anywhere that
diffed old text against new. The single human approval gate was shown a table with the columns
`Document | Tokens | Score | Grade | Edit` — a Yes/No flag, no before/after size, and no sample of
what "Yes" would remove. Approval came 112 seconds later. **The owner approved roughly 57,000 tokens
of deletion without ever being shown a deletion.**

### Findings by severity

| Severity | Count |
|---|---:|
| P1 | 8 |
| P2 | 53 |
| P3 | 43 |
| **Total** | **104** |

One additional structural fact: the P1 about the DirectXTK `Keyboard` rejection was found
independently by **two** auditors (G03 Input and G11 ThirdParty), because it was deleted from both
of the documents that recorded it. That knowledge now exists nowhere in the repository — not in a
document, not in a source comment.

## 5. P1 findings

Eight findings, worst first. Each gives the file, what was lost or wrongly asserted, `file:line`
code evidence, the concrete bad change a future editor would make, and replacement text ready to
paste.

---

### P1-1 — Players navigation document asserts the opposite of a deliberate RNG mirror, and following it desyncs client and server

- **File:** `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/AGENTS.md`
- **Category:** introduced inaccuracy
- **Source report:** G10 (F-G10-01)

This is the only case in the entire audit where the rewrite asserted something **false** rather than
merely dropping something true, which is why it leads.

**Deleted text:**

> "Mode-4 and mode-5 RNG consumption is held identical so mode flips (mode 5 → 4 zeroes the
> destination at the same tick, triggering mode 4's entry-block draws) do not desync the shared
> random engine: each draws exactly three randoms (island pick + footprint X + footprint Y, in that
> order)."

**Current replacement (line 11):**

> "Modes 4 and 5 do not have an identical per-tick draw schedule; parity comes from both builds
> evaluating the same shared-state conditions."

**Code evidence:**

- `PlayersNavigation.cpp:304-312` — mode 5 executes three otherwise-unused draws, with the comment
  "Mirror mode 4's three entry draws (island pick + footprint X + footprint Y) so flipping between
  modes 4 and 5 does not desync the shared random stream. Only the draw COUNT matters".
- `PlayersNavigation.cpp:344-361` — mode 4's matching three-draw entry block.
- `PlayersNavigation.cpp:224-232` and `PlayersNavigation.cpp:247-251` — the mode 5 → 4 flips the
  mirror exists for.

**The bad change:** mode 5 draws an island index and two footprint values it immediately discards
(it even reads a placement by `i % islands.size()` purely to have bounds). A future editor cleaning
up "dead RNG" is told by this document that no cross-mode draw-count contract exists and that parity
comes from shared-state conditions alone. Deleting those three draws produces a client/server
desync on every follow → island mode flip — the most expensive bug class in this repository. The
document and the code comment directly contradict each other, and the document is the wrong one.

**Recommended action:** rewrite.

**Proposed text:**

```markdown
- Random draws, arrival checks, and mode transitions stay outside the pathfinding throttle. Island-destination and flagship-follow modes must consume the same number of draws on a tick they can flip between: the follow mode mirrors the destination mode's three entry draws (island pick, footprint X, footprint Y) every tick, and only the draw count matters because every `common::Random` advances the engine once regardless of its bound. Those mirror draws are load-bearing, not dead code.
```

**The document now contradicts itself, and the later commit is the correct half.** This is decisive
new evidence, and it narrows the fix considerably. At `HEAD` the same file says both of these:

- **Line 11** — written by Sol in `280e89b7`, unchanged since: "Random draws, arrival checks, and
  mode transitions remain outside the pathfinding throttle. Modes 4 and 5 do not have an identical
  per-tick draw schedule; parity comes from both builds evaluating the same shared-state
  conditions."
- **Line 8** — rewritten later by `1c6a6ce5` on 07-28: "…this preserves mode 4's three-draw schedule
  and leaves the fleet's 60-second rally timer unchanged."

So one document simultaneously denies that a fixed draw schedule exists and depends on one three
lines earlier. Line 8 agrees with the code at `PlayersNavigation.cpp:304-312`; line 11 does not.

Consequences:

- **Attribution is confirmed.** The false sentence is the rewrite's, not later drift. It is present
  in `280e89b7` and has never been edited since. Verified by comparing the file at `280e89b7`,
  `1c6a6ce5`, `887a6be5`, and `HEAD` — `887a6be5` touched only this file's CRC bullet, and neither
  of the other two commits touched line 11.
- **The fix is now unambiguous and low-risk.** Line 11's second and third clauses are the wrong
  half and should be replaced; its first clause (draws and transitions sit outside the throttle) is
  correct and stays. **Line 8 must be left alone.** This is a worked example of the preservation
  rule in section 10: the naive repair — rewriting the whole bullet list from the proposed text
  above — would destroy `1c6a6ce5`'s correct D+2 recovery description in line 8.
- **The contradiction is itself process evidence.** Two statements this directly opposed sat three
  lines apart for days without anyone reconciling them, which supports section 9's conclusion that
  nothing in the process checks retained text against its neighbouring text.

---

### P1-2 — A client-only member added to a `Members()`-only collection silently enters CRC and wire format; the warning was deleted while live code still cites it

- **File:** `Engine/Source/Frame/Collections/AGENTS.md`
- **Category:** lost invariant
- **Source report:** G04 (F-G04-02)

**Deleted text:**

> "**Never add a `#if defined(BT_CLIENT)` member to a `Members()`-only leaf** — it would be CRC'd
> and wire-serialized into the determinism path with no compile error (the parity `static_assert`
> fires only on collections that define `SharedMembers()`). Give the leaf a `SharedMembers()` first
> (with the client field in `ClientMembers()`) … (e.g. `SpaceshipsPostRender`)."

**Current text keeps only:** "Client-only graphics resources and render hooks remain under narrow
`BT_CLIENT` guards without changing shared layout."

**Code evidence:**

- `Engine/Source/Frame/Collections/Collection.h:389-396` — `SharedCollectionCrc` guards the parity
  `static_assert` inside `if constexpr (HasSharedMembers<TStruct>)`, so a collection without
  `SharedMembers()` CRCs its full `Members()` with no diagnostic.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.h:141-145` — still
  carries a code comment defining `SharedMembers()` as its full set precisely "(Collections hub
  rule)", a reference to text that no longer exists in the hub.

**The bad change:** adding one `#if defined(BT_CLIENT)` column to a `Members()`-only leaf compiles
cleanly on both builds and then desyncs the CRC and shears the server broadcast. The failure is
silent. A live source comment pointing at the deleted rule proves the rule was load-bearing.

**Recommended action:** restore condensed.

**Proposed text:**

```markdown
- Never add a `#if defined(BT_CLIENT)` member to a collection that declares only `Members()`: it is then CRC'd and wire-serialized into the determinism path with no compile error, because the server parity `static_assert` fires only on collections that declare `SharedMembers()`. Split the leaf into `SharedMembers()` plus `ClientMembers()` first. A leaf with no client fields yet can pre-empt the trap by declaring `SharedMembers()` as its full set and forwarding `Members()` to it (`SpaceshipsPostRender`).
```

---

### P1-3 — Collision event ordering and commit contract deleted; nothing else in the repository states it

- **File:** `Engine/Source/Frame/AGENTS.md`
- **Category:** lost invariant
- **Source report:** G04 (F-G04-03)

**Deleted text:**

> "**Collision** - Layer-based spatial partitioning … Candidate events are collected across every
> layer pair, sorted by time of impact then stable layer/object keys, and committed symmetrically; a
> destroy-on-collide object accepts at most its earliest event. Optional exclusive maximum-time
> cutoffs reserve terrain and frame-boundary ties for callers. Per-pair masks must be bi-directional
> (asserted); same-layer collision unsupported."

**Code evidence:**

- `Engine/Source/Frame/Collision.cpp:391-395` — sorts candidates by
  `std::tie(fTimeOfImpact, uiLayerA, iObjectA, uiLayerB, iObjectB)`. The trailing keys exist purely
  to make the order total and reproducible, and no comment there says so.
- `Engine/Source/Frame/Collision.cpp:530-540` — destroy-on-collide earliest-event acceptance.
- `Engine/Source/Frame/Collision.cpp:313` — asserts "Same-layer collision not implemented".
- `Engine/Source/Frame/Collision.cpp:326-327` — asserts bi-directional masks.
- Grepping every `AGENTS.md` finds no surviving statement of the ordering rule. The only related
  survivor is a consumer-side sentence at
  `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/AGENTS.md:7`.

**The bad change:** collision results feed CRC'd sim state. The stable tiebreak keys look like
incidental noise, so an editor optimizing the sort — sorting by time only, switching to an unstable
partial sort, or committing per layer pair instead of globally — would silently make outcomes depend
on layer-registration order and thread scheduling. That produces a client/server desync visible only
as a CRC mismatch. This is the determinism-critical directory, and its own hub now says nothing
about collision beyond "queues are thread-local".

**Recommended action:** restore condensed.

**Proposed text:**

```markdown
- Collision collects candidate events across every layer pair, then sorts them globally by time of impact with layer and object indices as tiebreakers so the order is total and reproducible, and commits accepted events symmetrically. A destroy-on-collide object accepts only its earliest event. Optional exclusive maximum-time cutoffs let callers reserve exact-time ties for terrain and frame-boundary outcomes. Per-pair masks must be bi-directional and same-layer collision is unsupported; both are asserted.
```

---

### P1-4 — The dual-dt `Update` contract was deleted and has no surviving owner

- **File:** `Engine/Source/Frame/Collections/AGENTS.md`
- **Category:** lost invariant
- **Source report:** G04 (F-G04-01)

**Deleted text:**

> "**Dual-dt Update contract**: the same `FrameInterpolate::Update` fan-out serves sim phase 1 at
> fixed dt inside `RunFrameTick` (`kfDeltaTime`) and client render interpolation at variable dt
> (`GameBase::Render` — can be 0.0 while snapshots buffer); inside `Update` the regimes are
> indistinguishable … Every interpolate-phase `Update` must be safe under both: no once-per-tick
> assumptions, correct at any dt including zero"

**Code evidence:**

- `Projects/BrokenEngineSandbox/Source/Frame/FrameTick.cpp:67` calls
  `FrameInterpolate::Update(rNext.interpolate, rCurrent, kfDeltaTime)`.
- `Engine/Source/GameBase.cpp:534-535` calls the same entry point per render frame with
  `fCoordDeltaTime`; `Engine/Source/GameBase.cpp:531` forces that value to `0.0f` for
  under-populated coords.
- `Engine/Source/Frame/FrameUtils.h:61-68` fans the call out to every collection with no comment
  distinguishing the two regimes; `Engine/Source/Frame/FrameBase.cpp:185-194` only stores
  `fDeltaTime`.
- No `AGENTS.md` in the repository now states the contract
  (`Projects/BrokenEngineSandbox/Source/Frame/AGENTS.md` covers phase order only).

**The bad change:** an author writing a new interpolate-phase `Update` naturally assumes it runs once
per tick — decrementing a cooldown, advancing a counter, dividing by a nonzero dt. On the client
that body runs at render rate and can receive dt == 0.0, producing per-framerate visual drift,
divide-by-zero, and, if the value feeds a shared column, CRC desync. The G04 auditor called this the
single most load-bearing rule the hub owned.

**Recommended action:** restore condensed.

**Proposed text:**

```markdown
- The interpolate-phase `Update` fan-out serves two regimes: fixed-dt simulation inside `RunFrameTick`, and client render interpolation at variable dt that can be 0.0 while snapshots buffer. `Update` cannot tell them apart. Every implementation must therefore be correct at any dt including zero, with no once-per-tick assumptions such as counters, cooldowns, or spawns.
```

---

### P1-5 — The GLSL `inverse()` prohibition lost the only statement of why it exists, and the scoping that prevents a wrong investigation

- **File:** `Engine/Data/Shaders/AGENTS.md`
- **Category:** lost invariant
- **Source report:** G08 (F-G08-01); the process side is also transcript red flag 4

**Deleted text:**

> **NVIDIA driver bug**: never call `inverse()` on mat3/mat4 in shaders — NVIDIA's compiler hangs
> indefinitely during pipeline creation. Precompute inverse matrices on the CPU and pass via
> buffers. This is the sole confirmed trigger — large `mat4[]` arrays are not a cause: the engine
> dynamically indexes the large runtime-sized `mat4 jointMatrices[]` SSBO hang-free. The
> joint-matrix buffer split is a layout choice (no embedded per-mesh joint cap), independent of this
> bug.

**Current text (`Engine/Data/Shaders/AGENTS.md:22`):**

> Do not call GLSL `inverse()` on matrices. Supply inverse transforms from the CPU or use a proven
> family-specific alternative.

**Code evidence:**

- A repository-wide search for `inverse()` outside `Documents/Plans` returns four hits:
  `Engine/Data/Shaders/AGENTS.md:22`, `Engine/Data/Shaders/AGENTS.md:34`,
  `.agents/skills/glsl-review/references/shader-footguns.md:127`, and
  `.agents/skills/glsl-review/SKILL.md:78`. None states the symptom, the vendor, or the non-trigger.
- The disambiguation is still factually live: `Engine/Data/Shaders/Model/ModelCommon.h:75-78`
  declares the runtime-sized `JointMatrix jointMatrices[]` SSBO and `Model/ModelSkinned.vert`
  indexes it dynamically.
- `Model/AGENTS.md` also lost its cross-reference to the bug.

**The bad change, two of them:**

1. A rule with no reason reads as a style or performance preference. An editor who needs a
   per-fragment inverse and cannot find a reason will use `inverse()` and get an indefinite hang
   inside `vkCreateGraphicsPipelines` — no error message, no validation-layer output, no obvious
   connection to the shader.
2. Someone debugging such a hang, or reviewing the split joint-matrix buffer, will re-litigate large
   `mat4[]` arrays as the suspect. The deleted sentence records that this was already investigated
   and ruled out, and that the buffer split is an unrelated layout choice.

**Process note:** the transcript shows this text was deleted deliberately, under the test "the
absolute 'sole confirmed trigger' / large-array 'hang-free' claims cannot be established from
current source." That is the wrong test for a recorded empirical finding — debugging results are by
construction not re-derivable by reading code.

**Recommended action:** restore condensed.

**Proposed text:**

```markdown
- Never call GLSL `inverse()` on a `mat3`/`mat4`: NVIDIA's shader compiler hangs indefinitely during pipeline creation, with no error surfaced. Supply inverse transforms from the CPU or use a proven family-specific alternative. This call is the sole confirmed trigger — large `mat4[]` arrays are not: the engine dynamically indexes the runtime-sized `jointMatrices[]` SSBO hang-free, and the joint-matrix buffer split is an unrelated layout choice.
```

---

### P1-6 — DirectXTK `Keyboard` was evaluated and rejected; the reasons are now recorded nowhere (found independently by two auditors)

- **Files:** `Engine/Source/Input/AGENTS.md` (primary) and `ThirdParty/AGENTS.md`
- **Category:** lost invariant
- **Source reports:** G03 (F-G03-01, rated P1) **and** G11 (F-G11-01, rated P2)

**This finding was raised independently by two auditors working different subsystems, because the
same decision was deleted from both documents that recorded it. It therefore now exists nowhere in
the repository — not in any `AGENTS.md`, and not in any source comment.** The two auditors graded it
differently (P1 from the Input side, P2 from the ThirdParty side); it is carried here at the higher
severity. See section 12 for the note on that severity divergence.

**Deleted from `Engine/Source/Input/AGENTS.md`:**

> "Kept in-house deliberately — DirectXTK `Keyboard` evaluated and rejected: adopting it would lose
> `RIDEV_NOLEGACY` (Alt+F4 reroutes to `WM_CLOSE`, bypassing the game `kQuit` binding), let ImGui
> consume WM_KEY*/WM_CHAR via `WantCaptureKeyboard`, and break generic
> `VK_MENU`/`VK_SHIFT`/`VK_CONTROL` bindings (DirectXTK sets only L/R-specific VKs)."

**Deleted from `ThirdParty/AGENTS.md`:**

> "`Keyboard` is deliberately not compiled — evaluated and rejected; the in-house Raw Input path's
> `RIDEV_NOLEGACY` semantics are load-bearing (see `Engine/Source/Input/AGENTS.md`)."

**Code evidence (all three reasons still hold):**

- `Engine/Source/Input/RawInputManager.cpp:43-50` registers the keyboard HID with `RIDEV_NOLEGACY`.
- `Projects/BrokenEngineSandbox/Source/Input/Input.cpp:37` binds quit as
  `rRawInput.pKeyboardKeys[VK_MENU] && KeyboardPressed(VK_F4, ...)` — a *generic* `VK_MENU`, exactly
  what DirectXTK would not set.
- `Engine/Source/Input/RawInputManager.cpp:259-261` writes the scratch array straight from raw-input
  virtual keys.
- `ThirdParty/Prebuilts/Source/Engine/DirectXTK.cpp:9-12` compiles exactly `AudioEngine.cpp`,
  `SoundCommon.cpp`, `GamePad.cpp`, and `Mouse.cpp` — no `Keyboard.cpp`.
- A repository-wide search for `Keyboard` across all `AGENTS.md` files returns **zero** hits; a grep
  for "DirectXTK" in `RawInputManager.cpp` returns nothing.

**The bad change:** the input layer mixes hand-rolled Raw Input (keyboard) with DirectXTK (mouse,
gamepad), and `Engine/Source/Input/AGENTS.md:7,10` presents DirectXTK as the normal Win32 message
path. That asymmetry now looks like an inconsistency worth cleaning up. Two routes to the same
break: an editor unifies the keyboard onto DirectXTK `Keyboard`, or an editor adds
`Src/Keyboard.cpp` to the existing unity unit alongside `Mouse.cpp`. Either silently breaks the
Alt+F4 quit binding and every generic-modifier binding, and reintroduces a legacy-message keyboard
consumer under `RIDEV_NOLEGACY` — a dead or half-working input path and a second source of truth for
keyboard state. The failure shows only at runtime.

**Recommended action:** restore condensed in **both** documents.

**Proposed text for `Engine/Source/Input/AGENTS.md`:**

```markdown
- Keyboard handling is deliberately hand-rolled rather than DirectXTK `Keyboard`. Adopting it would lose `RIDEV_NOLEGACY` (Alt+F4 would reroute to `WM_CLOSE` and bypass the game quit binding), let ImGui capture keyboard messages, and break generic `VK_MENU`/`VK_SHIFT`/`VK_CONTROL` bindings, because DirectXTK reports only the left/right-specific virtual keys. Do not unify the keyboard onto DirectXTK to match the mouse and gamepad paths.
```

**Proposed text for `ThirdParty/AGENTS.md`** (append to "Adaptation and Consumption"; this also
gives the deliberate-exclusions delegation in P2-G11-02 a real home):

```markdown
Deliberate exclusions are load-bearing, not oversights: DirectXTK's `Keyboard` is evaluated and rejected because the in-house Raw Input path's `RIDEV_NOLEGACY` semantics own keyboard state (`Engine/Source/Input/AGENTS.md`); bc7enc_rdo's `bc7e.ispc` encoder stays off so bakes reproduce; Clipper2 runs on the shared sim path through its integer-quantized precision.
```

---

### P1-7 — Hard device dependency on `COLOR_ATTACHMENT_BLEND_BIT` deleted, along with the rule to extend the guard

- **File:** `Engine/Source/Graphics/Managers/AGENTS.md`
- **Category:** lost invariant
- **Source report:** G05 (F-G05-01)

**Deleted text:**

> "Boot fails loud (ASSERT + error log) if the device does not advertise
> `COLOR_ATTACHMENT_BLEND_BIT` for the special-format RTTs the MAX/ADD-blended prepasses
> (elevation/lighting/smoke/wind) target — a hard device dependency, unlike the TextureManager
> linear-filter probe which downgrades gracefully. A new blended prepass on a novel format must
> extend that guard."

**Code evidence:**

- `Engine/Source/Graphics/Managers/InstanceManager.cpp:400-412` still runs the guard over
  `pBlendedRenderTargetVkFormats {keElevationFormat, keLightingFormat, keSmokeFormat, keWindFormat}`,
  with the comment "absent it is silent UB at pipeline creation".
- The contrasting graceful downgrade still exists (`TextureManager.cpp` sampler filter probe,
  documented at `Engine/Source/Graphics/Managers/TextureManager.AGENTS.md:9`).
- No `AGENTS.md` mentions the guard today. The `InstanceManager` leaf never carried it, and the
  current leaf (`InstanceManager.AGENTS.md:5`) says only "format-specific optimal-tiling support",
  which is `ValidatePhysicalDeviceCapabilities`, not the blend check.

**The bad change:** the obligation is discharged in a file nobody edits when adding a render target.
An engineer adding a new MAX/ADD-blended prepass on a new special format works in
`RenderTargetTextures.cpp` and the shader tree, ships without extending the format array, and gets
silent undefined behaviour at pipeline creation on any device that does not advertise the bit — with
no boot diagnostic.

**Recommended action:** restore condensed.

**Proposed text:**

```markdown
- Boot fails loud if the device does not advertise `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT` for every special-format render target the MAX/ADD-blended prepasses write (elevation, lighting, smoke, wind). Blending without it is silent undefined behaviour at pipeline creation, so this dependency is hard, unlike the sampler linear-filter probe that downgrades gracefully. Adding a blended prepass on a new format must extend that guard.
```

---

### P1-8 — The audio document now states an unqualified rule that shipping code visibly violates, and the caveat that made it safe was deleted

- **File:** `Engine/Source/Audio/AGENTS.md`
- **Category:** lost invariant
- **Source report:** G03 (F-G03-02)

**Deleted text:**

> "`Clear` destroys *while holding* the mutex; safe only because `OnBufferEnd` is a bare atomic
> increment that never takes it (path-specific, not a class-wide invariant)."

**Current text states only the general rule:** "Destroy faded streams after releasing the streaming
mutex so source-voice teardown cannot deadlock callback completion."

**Code evidence:**

- `Engine/Source/Audio/StreamingVoices.cpp:137` takes `mMutex`; `:163-165`
  (`mpCurrentStream.reset(); mPreviousStreams.clear(); mStreamsToDestroy.clear();`) destroys
  `StreamingVoice`s inside that scope, which ends at `:168`.
- `StreamingVoice::~StreamingVoice` (`StreamingVoice.cpp:24-27`) calls
  `DestroyXAudio2SourceVoice`, which calls `AudioEngine::DestroyVoice` (`AudioUtility.h:34`) and can
  block waiting on callbacks.
- The only reason this cannot deadlock is `StreamingVoice::OnBufferEnd`
  (`StreamingVoice.cpp:204-207`), a bare `miBuffersConsumed.fetch_add`.
- Contrast `StreamingVoices.cpp:128-130`, where the deferred-destruction drain deliberately runs
  after the lock scope. There is no source comment on `Clear` recording any of this.

**The bad change:** the document states an unqualified rule that one shipping code path visibly
breaks. A reader either "fixes" `Clear` to match the document, or — worse — adds mutex-taking work
to `OnBufferEnd` (a natural place to nudge the fill worker) and produces an intermittent audio-thread
deadlock on device reset and suspend.

**Recommended action:** restore condensed.

**Proposed text** (append to the existing bullet in `## Runtime Contracts`):

```markdown
`Clear` is the deliberate exception: it destroys under the mutex, and that is safe only because the XAudio2 buffer callback is a bare atomic increment that never takes the mutex. Keep that callback lock-free.
```

## 6. P2 findings

Fifty-three findings, grouped by subsystem. Each carries the path, what was lost, `file:line`
evidence, and the recommended action.

### 6.1 Common (`Common/**`) — 6 findings

**C-1. Allocation-tracking suppression described as "global"; the counter is `thread_local`.**
`Common/AGENTS.md`. Introduced inaccuracy. Deleted: "**AllocationTracking.h** - Global-namespace
(not `common::`) thread-local suppression counter + two RAII guards…". Current line 22 says
"Allocation tracking is controlled by the global `ScopedSuppressAllocationTracking` guard."
Evidence: `Common/AllocationTracking.h:4` —
`inline thread_local int64_t giAllocationTrackingSuppressed = 0;`; guards at `:8` and `:18` mutate
only that thread's copy. Impact: an editor wrapping a `gpMultithreading->Dispatch()` call site
(`Common/Threading/Multithreading.h:24`) expecting suppression to cover the dispatched workers gets
`DEBUG_BREAK()` on every allocating worker thread. **Action:** rewrite to say the guard is
global-namespace but per-thread, and does not cover dispatched workers.

**C-2. `_Analysis_assume_` rationale deleted, documented nowhere else.** `Common/AGENTS.md`. Lost
invariant. Deleted: "Each also carries a no-codegen `_Analysis_assume_` post-condition so Release
`/analyze` (PREfast) models the asserted predicate as holding on fall-through (kills C6011-class
warnings on the `ASSERT(p); p->...` pattern)…". Evidence: `Common/ErrorUtils.h:12-14` — all three
macros end in `_Analysis_assume_(...)` with no explanatory comment; a repository grep finds only the
macro bodies and `Engine/Source/Graphics/GraphicsUtils.h`. Impact: `_Analysis_assume_` looks like
dead code; deleting it re-floods the Release `/analyze` build with C6011 warnings, and a new
validation macro written without it does the same. **Action:** restore condensed — append to the
validation-macro bullet that each carries a no-codegen `_Analysis_assume_` post-condition and any
new validation macro needs the same.

**C-3. Rule for validating `.pack` fields against fixed structural maxima deleted.**
`Common/AGENTS.md`. Lost specificity. Deleted: "`.pack` parsers with fixed structural maxima bound
against the `DataFile.h` `kiMax*` constants instead." Evidence: still the only bound on those fields
— `Engine/Source/Graphics/AnimationData.cpp:65-69`,
`Engine/Source/Graphics/Objects/ModelPipeline.cpp:66-67`; constants at `Common/DataFile.h:90-91`,
`:162`, `:185-186`, `:210`, `:232`. Impact: the surviving sentence points every untrusted count at
`ValidateDeserializedCount*`, which bounds against stream length only; a `.pack` header field such
as `uiTextureCount` passes that check and still overruns a fixed `kiMaxTextures`-sized buffer.
**Action:** restore the one deleted sentence verbatim.

**C-4. Ban on format specs at allocation-tracked `LOG` call sites deleted; now stated nowhere.**
`Common/AGENTS.md`. Lost specificity. Deleted: "…allocation-tracked Game/Engine `LOG` call sites
must not request those specs; `/repo-code-review` owns the exact accepted call-site forms."
Evidence: `Common/Log/LogFormatters.h:9-11` notes only that the custom formatters honor specs
allocation-free; `.agents/skills/repo-code-review/SKILL.md:153-154` — the document the root
`AGENTS.md` names as owner — no longer carries the rule, so the delegation dead-ends. Zero `LOG(`
call sites under `Engine/Source` or `Projects` use any `{:…}` spec, so the codebase observes a rule
it no longer states. **Action:** restore condensed. **Caveat carried forward, do not upgrade:** the
auditor verified the deletion, the missing owner, and universal compliance, but **could not verify
from repository source that a `{:…}` spec on a built-in MSVC formatter actually heap-allocates** —
that lives in the STL, not this repository. Confirm the mechanism before restoring "must not" as an
absolute.

**C-5. Common's reusable-utility index removed with no replacement pointer.** `Common/AGENTS.md`.
Lost specificity. Deleted: bullets naming `AlignedUniquePtr`/`MakeAligned`/`AlignedDeleter`
(64-byte-aligned SIMD storage), `Smoothed<T, COUNT>`, `InTheLastSecond`/`Timer`, `ScopedLambda`, and
the `StringUtils.h`/`FileUtils.h` helpers. Evidence: all still exist — `Common/AlignedMemory.h`,
`Common/Smoothed.h`, `Common/Timer.h`, `Common/ScopedLambda.h`, `Common/StringUtils.h`,
`Common/FileUtils.h`, aggregated through `Common/Common.h`. Impact: the root directive is "reuse
existing mechanisms" and this hub was the only index of what Common offers; without it an agent
writes its own `alignas` allocator or scope guard. This is the reuse surface, not a member/enum
inventory. **Action:** restore as one condensed bullet listing the headers and their purposes,
pointing at `Common.h` as the full index.

**C-6. Ban on `std::random` in gameplay code lost.** `Common/Math/AGENTS.md`. Lost invariant.
Deleted from the hub: "All gameplay randomness flows through this — never `std::random`." The new
`Math/AGENTS.md:13` says only "`RandomEngine` is the deterministic gameplay RNG." Evidence:
`<random>` is a live PCH include (`Common/ExternalHeaders.h:95`) so `std::mt19937` compiles
anywhere; a repository-wide grep for `mt19937|uniform_int_distribution|uniform_real_distribution|random_device|std::shuffle`
returns nothing, so the ban is observed but unwritten. `Documents/FloatingPointDeterminism.txt:87-95`
describes the Xorshift engine but never forbids the alternative; `Common/Math/Random.h:11-32`
documents seeding and the CRC contract, not the prohibition. Impact: standard-library distributions
have implementation-defined output, so one in sim code breaks bit-determinism across toolchains and
desyncs the per-tick CRC, surfacing only as a rare replay divergence. **Action:** restore as one
clause on `Math/AGENTS.md:13`.

### 6.2 DataPacker (`DataPacker/**`) — 3 findings

**D-1. Migration section drops "IBL half-floats are legacy-shaped on purpose" and mislabels the
scanned tree.** `DataPacker/Source/ExportJobs/Texture/AGENTS.md`. Lost invariant plus introduced
inaccuracy. Current text: "`MigrateLegacyIntermediates()` runs before readers consume cached
intermediates. It is idempotent: marked files are skipped, while valid unmarked files are rewritten
to the current header/encoding contract." Evidence:
`DataPacker/Source/ExportJobs/Texture/MigrateLegacyIntermediates.cpp:6-9` — "The IBL
`.R16G16B16A16_SFLOAT` intermediates are excluded on purpose -- they are legacy-shaped by design and
must never be picked up by the migration pass"; `:10-30` the inverse map returns
`VK_FORMAT_UNDEFINED` for everything except BC4/BC5/BC7/R16, `:121-125` returns early on that;
`ExportCubemapIbl.cpp:215-235` — the IBL writer emits no magic qword, so those files *are* "valid
unmarked files"; `:224-235` — the pass walks `gpFileManager->mpInputDirectories` (the tracked asset
trees), not the `%LOCALAPPDATA%` cache, rewriting in place at `:190-200`/`:215`. Impact: the rule as
written describes exactly the files that must never be touched; extending the format map to "all
intermediates" would zlib-wrap and re-header raw half-float cube faces and silently break IBL. The
doc also hides a non-transactional in-place rewrite of tracked files. **Action:** rewrite the
Migration paragraph to name the tracked-tree scope, the in-place rewrite, and the deliberate BCn/R16
scoping with the IBL exclusion.

**D-2. Audio section compressed past naming its inputs or its owner.**
`DataPacker/Source/ExportJobs/AGENTS.md`. Over-compression. Deleted: the accepted source formats
(16-bit PCM and 32-bit float `.wav`), the bit-exact 48 kHz round-trip, the mastering-rate lockstep,
and "Policy toggles live in `AudioRepair.h`". Evidence: `ExportAudio.cpp:33-53` — only
`WAVE_FORMAT_PCM`/16-bit and `WAVE_FORMAT_IEEE_FLOAT`/32-bit decode, everything else hits
`ASSERT(false)`; `:40-41` and `:75-81` — the `/32767.0f` divisor and `std::lround` exist for the
bit-exact round-trip; `AudioRepair.h:6-16` — `kiAudioExportSampleRate = 48000`, the mastering-rate
note, and both policy toggles. (The pass order at `AudioRepair.cpp:508-510` is well covered in
source and should not be restored.) Impact: a reader adding an audio asset cannot learn a 24-bit WAV
will assert, and a reader touching repair policy is not pointed at the file owning every toggle.
**Action:** restore condensed — accepted formats, the round-trip pairing, and the `AudioRepair.h`
pointer.

**D-3. Island underwater cut line no longer names the constant the runtime shares.**
`DataPacker/Source/ExportJobs/Island/AGENTS.md`. Lost specificity. Deleted the naming of
`common::kfUnderwaterMaskThresholdMeters`; current text says only "above-threshold shoreline".
Evidence: `Common/DataFile.h:26` —
`inline constexpr float kfUnderwaterMaskThresholdMeters = -2.5f;`;
`DataPacker/Source/ExportJobs/ExportIsland.cpp:201` (texture mask) and `:98` (valid-area hull) both
use it; `Engine/Source/Graphics/Render/GlobalUniforms.cpp:638` publishes the same constant to the
shaders; `Engine/Source/Graphics/Render/MainUniforms.cpp:104` uses it for the debug hull draw.
Impact: "above-threshold" reads as a DataPacker-local tuning knob, but it is one `common::` constant
defining the baked mask cut line, the hull's valid area, and runtime underwater shading
simultaneously. Introducing a second local threshold desynchronizes baked textures from the shader
with no compile-time signal. **Action:** restore condensed, naming the shared constant and the
single-sourcing requirement.

### 6.3 Engine core (`Engine/Source/{File,Input,Ui}`) — 5 findings

**E-1. The `Engine.h` aggregation exception for `RawInputManager.h` is documented nowhere.**
`Engine/Source/Input/AGENTS.md` (with `Engine/Source/AGENTS.md`). Lost invariant. Deleted from
Input: "game `Input.h` includes `RawInputManager.h` directly because `Engine.h` pulls it in only
inside the client span"; the hub's parallel exception list was deleted too, and the hub now says
exceptions are "noted at the subsystem hubs" while the Input hub no longer notes this one. Evidence:
`Engine/Source/Engine.h:28` opens `#if defined(BT_CLIENT)` and `:88` closes it, with the
`RawInputManager.h` include inside at `:74`; `Engine/Source/Input/RawInputManager.h:31` declares the
shared `struct RawInput` outside the guard, with `#if defined(BT_CLIENT)` only at `:45`;
`Projects/BrokenEngineSandbox/Source/Input/Input.h:3` includes `Input/RawInputManager.h` directly so
the server build sees `RawInput`. Neither header comments the reason. Impact: a tidy-up pass removes
the "redundant" direct include and breaks the server build, or moves the whole header inside the
client guard and breaks shared `FrameInput` code. **Action:** restore condensed into
`## Device and Frame Boundaries`.

**E-2. The pack integrity token vanished from the document that produces it.**
`Engine/Source/File/AGENTS.md`. Lost specificity. Deleted: "After validating manifests at startup,
it synchronously hashes the ordered Islands manifest table into the connection integrity token…"
Evidence: `Engine/Source/File/PackChunks.cpp:194-198` CRCs the ordered `ChunkLocation` table for
`kDataTypeIslands` into `mPackIntegrityToken`; consumed at
`Engine/Source/Network/Client/ClientSend.cpp:212`; gated at
`Engine/Source/Network/Server/ServerReceive.cpp:283-288`, which refuses the connection on mismatch.
The consumer side is documented at `Documents/Architecture/Network.md:78`; nothing in the File
document mentions it, and `PackChunks.cpp:197` has no comment. Impact: an editor who sorts, filters,
or re-emits the Islands chunk table for a good local reason silently makes every client fail the
connection handshake, with an error pointing at DataPacker rather than at this change. **Action:**
restore condensed into `## Packed Assets`, with the link to `Documents/Architecture/Network.md`.

**E-3. Texture-chunk reset understates what it rewrites, and its threading precondition is gone.**
`Engine/Source/File/AGENTS.md`. Lost invariant plus introduced imprecision. Deleted: "The transfer
thread must not upload a chunk being reset. Lock-free audio reads remain safe because restoration
rewrites identical pool pointers for chunks not being evicted." Current: "device loss and island
eviction reset GPU state without changing the lazy-pool layout." Evidence:
`Engine/Source/File/PackChunks.cpp:785-791` rewrites `iDataSize` and `pData` for **every** lazy chunk
on both reset paths — the layout is recomputed and merely lands on identical values for non-evicted
chunks; `:768-782` spells out the precondition as "relied upon, NOT enforced here", naming both
racing consumers (`TextureUploadManager::UploadThread` and the audio fill worker
`kThreadStreamingVoiceFill` via `ReadChunkData`) and ending "Keep the two callers' transition logic
in sync." Impact: the document reads as a narrow GPU-handle operation with no threading contract, so
a third caller (a new streaming-asset eviction) gets added without draining the upload thread — a
use-after-free on `vkImage` that reproduces only under load. **Action:** rewrite the bullet to state
the whole-map pointer rewrite, the caller obligation, and the sync requirement.

**E-4. The eager-chunk size trap was deleted.** `Engine/Source/File/AGENTS.md`. Lost specificity.
Deleted: "`EagerChunk.iDataSize` is the raw on-disk extent, including a scene chunk's appended
animation section that `ChunkHeader::iSize` excludes." Evidence:
`Engine/Source/File/PackChunks.cpp:341-343` sets `EagerChunk.iDataSize` from the chunk-table extent;
`:841-842` bounds-checks reads against it with a comment saying exactly why `pHeader->iSize` is
wrong for scene chunks. The current document says nothing about eager chunk extents. Impact:
`pHeader->iSize` is the obvious-looking field; a new eager consumer using it silently truncates
animation data — a data bug with no crash. **Action:** restore as one bullet under `## Packed Assets`.

**E-5. The live A/B combine-curve apparatus and its exit condition were deleted.**
`Engine/Source/Ui/AGENTS.md`. Lost invariant. Deleted: "The Lighting tab carries a deliberate A/B
curve-tuning apparatus — `gCombineCurveOld`/`gCombineCurveNew` plus the `gbUseCombineCurveNew`
toggle … Kept as two curves on purpose until tuning settles; collapse to one when done." Evidence:
all three symbols live and the only consumers of that shader input —
`Engine/Source/Ui/LightingWrappersBase.cpp:74-76` (both curves currently hold identical control
points), `LightingWrappersBase.h:75-77`,
`Engine/Source/Graphics/Render/LightingUniforms.cpp:320`
(`gbUseCombineCurveNew ? gCombineCurveNew : gCombineCurveOld`),
`Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenLighting.cpp:212-217`. No source comment records
why there are two. Impact: two byte-identical curves behind a bool look like dead duplication; one
editor deletes the "old" one and destroys the ability to A/B against the shipping baseline, another
leaves it forever because nothing says it is temporary. **Action:** restore condensed into
`## Shared Types`, keeping both the purpose and the exit condition.

### 6.4 Engine frame and collections (`Engine/Source/Frame/**`) — 7 findings

**F-1. Visual-spawn ID stream determinism rule lost from the hub.**
`Engine/Source/Frame/Collections/AGENTS.md`. Lost invariant. Deleted: "visual-only spawns must use
`id_t::GenerateVisual` / `uuid_t::GenerateVisual` (and `AddVisualIndexableElement`) so visual
creation does not perturb the main UUID counter. Transfer/reconnect re-adds use
`AddIndexableElementWithId` so the server-issued ID survives. Random calls must run unconditionally
on both builds even when the consuming spawn is client-only". Evidence: separate counters live at
`Engine/Source/Frame/FrameBase.h:191-192`,
`Engine/Source/Frame/Collections/CollectionId.h:29,69-71`,
`Engine/Source/Frame/Collections/Collection.h:528-536`. A repository-wide grep for `GenerateVisual`
across `AGENTS.md` returns nothing; the only related survivor is a sound-specific line in
`Engine/Source/Frame/Collections/Sounds/AGENTS.md:9`, and the random-lockstep half survives only in
the `Explosions` leaf. Impact: a client-only spawn calling the ordinary `AddIndexableElement`
advances the shared UUID counter on the client but not the server, so every subsequent
server-authoritative ID diverges. Rated P2 only because `AddVisualIndexableElement` sits beside its
plain sibling with an explanatory comment. **Action:** restore condensed.

**F-2. Frame purity constraint (sim vs render terrain sampling) dropped from the Frame hub.**
`Engine/Source/Frame/AGENTS.md`. Lost invariant. Deleted: the split between the sim hot path
(`FrameElevation`/`FrameNormal`, reading only the cell's own `FrameStaticData` grid, never
`mCoordFrames`, never crossing cells) and the render path (`GlobalElevation`/`GlobalNormal`, walking
`mCoordFrames` and never running from frame-tick code, assert-enforced). Current text keeps only
"Elevation is shared". Evidence: `Engine/Source/Frame/IslandTerrain.h:148-153` and `:167-171`
document both halves; asserts live at `Engine/Source/Frame/IslandTerrain.cpp:396-398` and `:580-583`
(`ASSERT(... !common::gpThreadLocal->mbInFrameTick)`);
`Documents/Features/Frame/FrameRelativePositions.md:30` still reasons from the constraint by name.
Impact: the split is what keeps per-cell parallel ticks from racing the tick-time grid build and
from reading a neighbour cell's state; a hub that says only "elevation is shared" invites a new sim
query to call the convenient global entry point. **Action:** restore condensed.

**F-3. Deliberate CPU/GPU elevation divergence flattened into a vague, partly wrong sentence.**
`Engine/Source/Frame/AGENTS.md`. Lost specificity plus introduced inaccuracy. Deleted:
"**Intentional GPU divergence** (do not 'fix' by syncing): `TerrainElevation.frag` applies an
undersea depth-compression `pow` curve neither CPU path mirrors … Visual-only: the sim never
consumes curved values and neither divergence affects CRC." Current line 25: "GPU elevation readback
uses the renderer's configured linear sampling path; CPU elevation uses packed height data. Do not
assert stronger external format guarantees here." Evidence:
`Engine/Data/Shaders/Terrain/TerrainElevation.frag:39-48` still applies
`pow(fT, globalLayout.fWaterUnderseaCompressionInv)` to undersea samples only, with no CPU
counterpart in `Engine/Source/Frame/IslandTerrain.cpp` (`CellElevation`, `:376-389`, folds raw
max-blended samples). Nothing is read back from the GPU — `mTerrainElevationTexture` is produced for
downstream shaders, so "readback" is also wrong wording. Impact: an editor noticing that CPU and GPU
terrain heights disagree underwater would "fix" it by porting the curve into `CellElevation`,
changing CRC'd sim positions for a purely visual effect. **Action:** rewrite.

**F-4. `thread_local` construction-time allocation ban and overflow policy lost.**
`Engine/Source/Frame/AGENTS.md`. Lost invariant. Deleted: "`Collision` and `AreaDamage`
thread_local vectors must start empty — constructors run during `mi_process_init` before the
allocator is ready. First-use resize and data-dependent overflow growth … wrap realloc in
`ScopedSuppressAllocationTracking` and (on overflow) `DEBUG_BREAK` naming the preallocate constant
to raise. Exception: the collision layer count is compile-time-determined … it logs `kError` and
asserts." Current text keeps only "Collision and area-damage queues are thread-local because coord
ticks may run in parallel." Evidence: `Engine/Source/Frame/Collision.cpp:101-102` ("thread_local
constructors run during mi_process_init before the allocator is ready, so pre-allocation would
crash"), `:49-52`, first-use resize under `ScopedSuppressAllocationTracking` at `:121-127` and
`:283-285`, overflow `DEBUG_BREAK` naming the constant at `:196-199` and `:337-340`, layer-count
exception at `:135-136`. A repository-wide grep for `mi_process_init` across `AGENTS.md` returns
nothing. Impact: the natural "optimization" is to pre-size these vectors at construction, which
crashes at process start before the allocator exists, with no useful stack. The rule applies to any
new thread-local sim scratch. **Action:** restore condensed.

**F-5. AreaDamage produce/consume phase window flattened to "preserve phase order".**
`Engine/Source/Frame/AGENTS.md`. Lost specificity. Deleted: "**AreaDamage** - Thread-local
accumulator … populated in PostCollision, queried in AreaDamage phase." Current: "Per-tick producers
and consumers must preserve phase order and clear their queues at the owning boundary." Evidence:
producer at
`Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.cpp:568-575`; consumer at
`Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/SpaceshipsCombat.cpp:219`; queue
cleared at the tail of that phase in `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp:445-456`
(and `Collision::Clear()` at the tail of PostCollision, `:442`). Impact: "preserve phase order" is
unactionable — it does not say which phase may add and which may query, so an `AreaDamage::Add` from
the AreaDamage phase itself, or a query from Update, silently produces a one-tick-late or dropped
hit. **Action:** restore condensed, naming both windows.

**F-6. SmokeTrails smoothed-position W == 0 sentinel deleted, and it contradicts the root W
invariant.** `Engine/Source/Frame/Collections/SmokeTrails/AGENTS.md`. Lost invariant. Deleted:
"W == 0 flags uninitialized and snaps instantly; smoothing rate is a hard-coded constant, not a
`gSmokeTrails*` tweak wrapper". Evidence:
`Engine/Source/Frame/Collections/SmokeTrails/SmokeTrailsUpdate.cpp:17-23` —
`if (XMVectorGetW(rCurrent.pVecSmoothedPositions[i]) == 0.0f)` snaps, else lerps;
`SmokeTrailsUpdate.cpp:8` `static constexpr float kfSmoothingRate = 20.0f;`. Impact: the root
`AGENTS.md` W invariant says positions carry W = 1.0; this column deliberately overloads W = 0 as
"uninitialized". An editor enforcing the root rule, or zeroing the column on spawn "for safety",
either breaks the first-frame snap or makes every existing trail snap. **Action:** restore condensed
— this is exactly the deliberate exception a leaf document exists to record.

**F-7. Render-only-state and three-phase render conventions deleted from the hub while leaves still
depend on them.** `Engine/Source/Frame/Collections/AGENTS.md`. Over-compression. Deleted: the
file-scope render-only state pattern keyed by `id_t<T>` with `EraseStaleRenderState<>()` pruning,
and the three-phase `BeginRender`/`Render`/`EndRender` protocol pre-sizing GPU buffers across all
active grid coords via `AccumulateRenderCapacity()`. Current hub keeps only "Rendering may publish
counters only where that collection actually owns them". Evidence: both helpers live at
`Engine/Source/Frame/Collections/Collection.h:607` and `:624`, with call sites in
`AreaLights/AreaLightsRender.cpp:28`, `Billboards/BillboardsRender.cpp:26`,
`HexShields/HexShieldsRender.cpp:27`, `PointLights/PointLightsRender.cpp:27`,
`Puffs/PuffsRender.cpp:25`, `SmokeTrails/SmokeTrailsRender.cpp:26`,
`WindRadials/WindRadialsRender.cpp:29`, `WindTrails/WindTrailsRender.cpp:35,38`. Leaves still
describe `BeginRender`/`EndRender` behaviour (`HexShields/AGENTS.md:11`,
`WindRadials/AGENTS.md:12`) with no hub definition of the phases they name. Impact: eight of nine
renderable engine collections follow one buffer-sizing protocol; with the hub silent, a new
renderable collection sizes its buffer from one coord's count and overflows as soon as a second grid
cell activates. Deleting the render-only-state pattern removes the sanctioned alternative to putting
client-only previous-frame data into the CRC'd frame, leaving only the unsafe option. **Action:**
restore both as condensed bullets.

### 6.5 Engine graphics (`Engine/Source/Graphics/**`) — 5 findings

**G-1. Uniform-descriptor routing rule flattened to unactionable prose.**
`Engine/Source/Graphics/Objects/AGENTS.md`. Lost specificity. Deleted: "A pipeline reading the
per-frame global UBO uses `kGlobalLayoutUniformBuffers`, while one reading the per-frame main UBO
uses `kMainLayoutUniformBuffers`; other caller-supplied framebuffer-indexed uniform arrays use
`kPerCommandBufferUniformBuffers`. None use plain `kUniformBuffer`." Current: "preserve framebuffer
routing for per-frame global, main, and pipeline data." Evidence: the four flags are live and
mutually exclusive at `Engine/Source/Graphics/Objects/Pipeline.h:35-41` and none carries an
explanatory comment, unlike the neighbouring `kSamplerBorderWhite`, `kSamplerAny`, and
`kBindlessArrayConsumer`; `Pipeline.cpp:43` documents only the set-indexing half. Impact: choosing
plain `kUniformBuffer` compiles and runs but binds a single non-framebuffer-indexed buffer, so the
pipeline reads a UBO the CPU is concurrently writing for another frame in flight — intermittent
one-frame-stale or torn uniforms, with no validation error. **Action:** restore verbatim, with the
"which would share one buffer across frames in flight" clause appended.

**G-2. Render-target sizing policy deleted, including the `maxImageDimension2D` cap and the fixed
water reference resolution.** `Engine/Source/Graphics/AGENTS.md`. Lost invariant. Deleted: the
shadow-block alignment, the 3840-pixel smoke reference, `WaterFullDetail`'s fixed 3840x2160 anchor
with the Gerstner rationale, and the `(maxImageDimension2D/3)*2` width cap. Evidence:
`Engine/Source/Graphics/Graphics.cpp:23` (`kiReferenceWidth = 3840`), `:54-64` (`WaterFullDetail`
anchors to 3840x2160 with the Gerstner rationale in comment), `:69` (smoke pixels scale off
`kiReferenceWidth`), and `Engine/Source/Graphics/Managers/RenderTargetTextures.cpp:66-81`
(`std::min(... * kfShadowHeadroomMultiplier, (iLimit / 3) * 2)` against `maxImageDimension2D`, plus
the even-width force). No `AGENTS.md` now mentions `maxImageDimension2D`, the 3840 reference, or
Gerstner-driven resolution independence. Impact, two traps: raising shadow quality or adding a wider
derived elevation variant without knowing the width is deliberately held at two thirds of the device
image limit fails image creation only on lower-limit devices; and "fixing" `WaterFullDetail` to
follow the live extent (the obvious cleanup, since `FullDetail` does) changes the water vertex grid
with resolution while Gerstner frequencies stay fixed, changing wave appearance per monitor. Both
rationales survive as code comments, so this is discovery loss. **Action:** restore condensed.

**G-3. Vulkan result-handling contract has no documentation home.**
`Engine/Source/Graphics/AGENTS.md`. Lost invariant. Deleted: "`CheckVkFailed` auto-escalates
swapchain/surface-loss results into destroy tiers; `DEVICE_LOST` throws `DeviceLostException`
(caught by main loop to recreate Graphics in place)" and "`CHECK_VK` … breaks once (after logging)
only on the fatal unexpected-result path; recoverable results … do not break". Evidence:
`Engine/Source/Graphics/GraphicsUtils.cpp:13-43` implements exactly this;
`GraphicsUtils.h:54` defines `CHECK_VK`; a repository grep across `AGENTS.md` for
"device lost"/"DeviceLost" finds only the one-line consequence at `Engine/Source/AGENTS.md:23`.
Impact: the current document asserts destroy tiers are monotonic but never says where tiers are
raised from, so a new Vulkan call site duplicates or bypasses the single escalation point, or adds a
`DEBUG_BREAK` on a recoverable path that is intentionally break-free — which would halt every debug
session on a routine window resize. **Action:** restore condensed.

**G-4. Render state-placement convention and the once-per-frame latch assumption deleted with no
code home.** `Engine/Source/Graphics/Render/AGENTS.md`. Lost invariant. Deleted: "Cross-file or
externally-reset state lives as `inline` globals in `Render.h`; single-file edge detectors / latches
stay function-local statics — do not promote. All latches assume the entry points run exactly once
per frame." Evidence: the split is intact — `inline` state at
`Engine/Source/Graphics/Render/Render.h:86-101` versus function-local or file-static latches at
`GlobalUniforms.cpp:417`, `LightingUniforms.cpp:116`, `WaterUniforms.cpp:104-124`. A grep for "once
per frame" / "do not promote" across `Engine/Source/Graphics/Render/*.cpp` returns nothing, so no
code comment carries the rule. Impact: promoting a private latch to `Render.h` makes it look
externally resettable and invites a second writer; and the reduced-time accumulators at
`WaterUniforms.cpp:104-124` integrate per call, so calling a population entry point twice in a frame
(an extra preview pass, a second `RenderFrameGlobal` for a capture) silently doubles water scroll
speed with no error anywhere. **Action:** restore verbatim.

**G-5. Two invisible pipeline-creation behaviours deleted.**
`Engine/Source/Graphics/Objects/AGENTS.md`. Lost invariant. Deleted: "Single-attachment pipelines
using the lighting render pass auto-upgrade to 3 color attachments with replicated blend state." and
"Indirect indexed draws use a negative omitted index-count sentinel for the whole buffer; an
explicit zero remains a zero-index draw, including valid empty material ranges." Evidence: the
sentinel at `Engine/Source/Graphics/Objects/Pipeline.cpp:342`
(`rCommand.indexCount = static_cast<uint32_t>(iIndexCount < 0 ? mInfo.pVertexBuffer->mInfo.iCount : iIndexCount);`),
with no comment on either branch; the blend replication at
`Engine/Source/Graphics/Objects/PipelineCreator.cpp:278-280` (`kiMaxColorAttachments = 6`,
`pMrtBlendStates[kiMaxColorAttachments]`) driven by `PipelineInfo::iColorAttachmentCount`
(`Pipeline.h:107`). The current document says only "Per-material draws preserve empty ranges".
Impact: passing `0` for "draw everything" (the intuitive default) silently emits a zero-index draw
and the geometry vanishes with no validation message; conversely a caller "normalising" a negative
count to zero breaks whole-buffer draws. A new lighting-pass pipeline author who hand-writes one
blend attachment expects an attachment-count mismatch, not a silent upgrade, and may add a second
conflicting mechanism. **Action:** restore both verbatim.

### 6.6 Graphics managers (`Engine/Source/Graphics/Managers/*.AGENTS.md`) — 2 findings

Structural note that applies to this whole group: these are **linked reference documents, not
directory memory**. `/update-claude-docs` excludes `*.AGENTS.md` from audit discovery, and the only
`CLAUDE.md` in the directory imports the hub `AGENTS.md`, not these. Their purpose is to hold the
detail the hub cannot. Every one of the eight was already inside the 2,000-token leaf target
(748–1,195) and was still cut 55–75%; today they measure 311–370 tokens. Restoring every finding
below would still leave each file under about 600 tokens. This is open question 1 in section 13.

**M-1. TextureManager's sub-object ownership map was deleted, and the three sub-objects have no
other documentation.** `Engine/Source/Graphics/Managers/TextureManager.AGENTS.md`. Lost specificity.
Deleted: the three-row table mapping `mTextureDescriptors` → `TextureDescriptors` (global descriptor
Set 0, bindless texture array, per-pipeline binding tracking, deferred descriptor updates),
`mTextureCache` → `TextureCache` (GPU-to-CPU image readback, file-based texture caching, PBR BRDF
LUT generation), `mRenderTargetTextures` → `RenderTargetTextures` (all effect render targets), plus
the whole `## TextureCache` section. Evidence: the three units are separate translation units and
none has its own document — `TextureDescriptors.h:1`, `TextureCache.h:1`,
`RenderTargetTextures.cpp:1`, `RenderTargetTexturesLighting.cpp:1`. The disk cache is live and
version-gated: `TextureCache.cpp:215` validates magic, version, format, extent, mips, layers, and
`sourceCrc` before reuse; `:257` stamps the version on write; `:104` is `GeneratePbrLutBrdf()`;
`:10` is the `CopyImageToHostMemory` readback. The current document says only "cached generated
textures". Impact: the next editor adding a generated texture (another LUT, a prefiltered
environment map) has no signal that a validated versioned on-disk cache already exists, and either
regenerates it every boot or hand-rolls a second caching path; and nobody can find which file owns
render-target creation or descriptor-set ownership. **Action:** restore a condensed `## Sub-Objects`
section naming the three units and the cache-invalidation contract.

**M-2. CommandBufferManager lost the reason per-frame uniform copies need no host-write barrier.**
`Engine/Source/Graphics/Managers/CommandBufferManager.AGENTS.md`. Lost invariant. Deleted: "Uniform
buffer copies rely on RecordCopy()'s internal post-copy barriers and vkQueueSubmit's implicit
host-write memory dependency." Evidence: both record-once command buffers copy their host-visible
layout buffers with no surrounding barrier of their own —
`CommandBufferRecordGlobal.cpp:36` and `CommandBufferRecordMain.cpp:45` call
`RecordCopy(vkCommandBuffer)`; `Buffer::RecordCopy` supplies the pre-copy barrier at
`Engine/Source/Graphics/Objects/Buffer.cpp:324-337` and the post-copy barrier at `:347-360`. Nothing
makes the CPU's writes to `mHostVisibleVkBuffer` visible explicitly; that comes only from the
submit-time implicit host-write dependency. A repository-wide grep for `host-write`/`HOST_WRITE`
under `Engine/Source/Graphics` returns only an unrelated comment in `Islands.h:23`, so the rationale
now exists in neither code nor documentation. Impact: command buffers are recorded once and reused
for the life of the swapchain while their uniform contents are rewritten every frame; an editor
auditing synchronization finds a per-frame host write with no `VK_ACCESS_HOST_WRITE_BIT` barrier,
cannot tell whether that is deliberate, and either adds a redundant per-frame barrier or concludes
the record-once scheme is unsafe and re-records. **Action:** restore condensed.

### 6.7 Network (`Engine/Source/Network/**`, `Projects/.../Network/**`) — 6 findings

Context that changes the reading of this group: `280e89b7` also landed the
`ClientSessionBase` → `ClientSessionRuntime` refactor, so a large share of the deleted text in the
three engine network files described classes that ceased to exist in the same commit. Much of what
looks like concision deletion is relocation to the new owner. **This group produced zero P1s** and
was judged the most disciplined part of the rewrite.

**N-1. `RecordContractViolation` may destroy the client record; the "do not touch the pointer" rule
is gone.** `Engine/Source/Network/Server/AGENTS.md`. Lost invariant. Deleted: "…at
`kiContractViolationDisconnectCount` pushes `PendingDisconnect {clientId, guid}` **itself** (so
fleet state persists) before `enet_peer_disconnect` + `RemoveClient`; **callers must not touch the
`ClientConnection*` after calling it**." Current: "contract violations use the central
accounting/disconnect path." Evidence: `Engine/Source/Network/Server/Server.cpp:510-543` —
`RecordContractViolation` ends in `enet_peer_disconnect(pPeer, 0); RemoveClient(iClientId);`; live
callers hold a `ClientConnection*` across the call at `Server.cpp:217`, `:228`, `:243`, `:248`,
`:264`, `:321`; the only surviving statement of the rule is a comment in the *game* layer at
`Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp:94-95`;
`Documents/Architecture/Network.md:51` documents the `PendingDisconnect` push but not the pointer
rule. Impact: a future editor adding a validation branch in `ServerReceive.cpp` would naturally
write `RecordContractViolation(...); pClient->somethingElse = …;` — a use-after-free reachable from
hostile client input. **Action:** restore condensed into `## Session Invariants`.

**N-2. LAN discovery protocol lost its only owner.** `Engine/Source/Network/AGENTS.md`. Lost
specificity. Deleted the whole `NetworkDiscoveryResponder`/`NetworkDiscoveryScanner` paragraph: raw
Winsock UDP (not ENet), non-blocking, port `kuiDefaultPort+1`, 4-byte magic `"BRKN"`, loopback ping
before LAN broadcast so a local server wins the race, `--loopback-only` symmetry, and the bare-magic
reply with no payload so the client connects to the responder's sender address on the default game
port. Evidence: `Engine/Source/Network/NetworkProtocol.h:177-178`
(`kuiDiscoveryPort = kuiDefaultPort + 1`, `kuiDiscoveryMagic = 0x42524B4E`);
`NetworkDiscoveryScanner.cpp:12` raw `socket(AF_INET, SOCK_DGRAM, …)`, `:53-58` loopback ping first,
`:60-67` LAN broadcast skipped under `kLoopbackOnly`, `:80-88` reply carries no payload;
`NetworkDiscoveryResponder.cpp:47-52` echoes the bare magic. A repository-wide `AGENTS.md` grep for
`BRKN`/`DiscoveryResponder` returns nothing, and `Documents/Architecture/Network.md` never mentions
discovery. Impact: discovery is a second, independent wire protocol whose two halves must agree, and
the loopback-first ordering is a deliberate race decision; with no owner, an editor changing the
reply to carry a port or payload has nothing telling them the client derives the game address from
the sender address, and nothing requiring `--loopback-only` symmetry. **Action:** restore condensed
into `## Ownership`.

**N-3. "No active slot resets `latestServerTick`" deleted with no surviving owner.**
`Engine/Source/Network/Client/AGENTS.md`. Lost invariant. Evidence:
`Engine/Source/Network/Client/ClientSessionRuntime.cpp:373-380` — `EvaluateClock` scans for any
`kActive` slot and, finding none, sets `miLatestServerTick = -1` and returns `0ns`; the consumer is
`Documents/Architecture/Network.md:19` (the hard sim ceiling is computed from `miLatestServerTick`),
and `ClientSessionRuntime.cpp:369` treats a negative value as "no clock yet". Neither `Network.md`
nor `GameReconciliation.md` states the reset. Impact: the reset looks like dead code in isolation;
removing it, or moving the active-slot check, leaves a stale `miLatestServerTick` driving the
ceiling clamp after every subscription drop, stalling the sim until the next subscription catches up
— exactly the freeze the ceiling slack exists to avoid. **Action:** restore condensed into
`## Transport Timing Inputs`.

**N-4. Fleets of disconnected clients keep running, and their reap pass is undocumented.**
`Projects/BrokenEngineSandbox/Source/Network/Server/AGENTS.md`. Lost invariant. Deleted: "fleets are
keyed by persistent GUID so they survive disconnect/reconnect; a separate reap pass handles
AI-managed disconnected fleets." Current keeps only "Fleets are keyed by persistent `ClientGuid`."
Evidence: `ServerFleetManager.cpp:383` `OnClientDisconnected` does not erase the fleet map entry;
`:469-520` `DetectDisconnectedPlayerDeaths` iterates fleets whose GUID has no live client id,
marks dead members, and shifts the flagship. The neighbouring current bullet ("A client is dead only
after all owned players are gone") is about connected clients only. Impact: the document now reads
as though disconnect ends server-side responsibility for that client's units; an editor adding fleet
teardown on disconnect breaks reconnect, or adding death handling only to the connected path leaks
dead-but-alive-flagged members that never shift the flagship. **Action:** restore condensed.

**N-5. Game-side paired wire codecs (`GameMessages.h`) have no document owner.** *(Staleness from
later commit `57f67d7f`, not from the rewrite.)*
`Projects/BrokenEngineSandbox/Source/Network/AGENTS.md`. Broken reference / lost specificity.
Current text still describes the pre-`57f67d7f` world: "Wire-sensitive event enums and
sender/receiver ordering move together." Evidence:
`Projects/BrokenEngineSandbox/Source/Network/GameMessages.h:1-40` defines `game::GameMessages`
payload structs with a single `Visit` descriptor and `static_assert`ed `kiSize`
(`AssignPlayerMessage` 16, `PlayerStateMessage` 17) that both sender and receiver route through,
added by `57f67d7f` ("Pair game wire format readers and writers"), which also rewrote
`PlayerEvents.cpp` and `ServerSession.cpp` onto it. No `AGENTS.md` mentions `GameMessages`. The
mirror-image engine statement *is* documented in `Engine/Source/Network/AGENTS.md`. Impact: an
editor adding a game packet would hand-roll cursor pushes in `ServerSession.cpp` and a matching
hand-rolled read in `PlayerEvents.cpp`, reintroducing exactly the drift hazard `57f67d7f` removed.
**Action:** rewrite the bullet to name `GameMessages` as the owner of game payload field order and
size.

**N-6. Post-transfer CRC recompute kept the rule but lost the reason.**
`Projects/BrokenEngineSandbox/Source/Network/Server/AGENTS.md` (mirrored in the client leaf). Lost
invariant (rationale). Deleted: "the frame tick computes `sharedCrc` before transfers land, so
`HarvestTransfers` re-runs `Frame::Crcs()` on every destination frame and logs pre/post CRCs —
clients validate against `GridUpdateData.sharedCrc`, so skipping this desyncs every transfer."
Current: "Recompute the destination Frame CRC after applying arrived transfers." Evidence:
`ServerTransferManager.cpp:243-250` snapshots each destination's pre-transfer
`postRender.sharedCrc`, `:261` `rDestFrame.postRender.sharedCrc = rDestFrame.Crcs();`, `:265` logs
the post value. The client mirror in
`Projects/BrokenEngineSandbox/Source/Network/Client/AGENTS.md` is also now reason-free. Impact: this
is the single ordering fact that makes cross-cell transfers non-desyncing; stated as a bare
instruction it looks like defensive recomputation and is a prime candidate for a "redundant CRC
pass" optimization. **Action:** restore the reason alongside the rule, on both sides.

### 6.8 Shaders (`Engine/Data/Shaders/**`) — 6 findings

**S-1. The precomputed ambient target's 0.25 averaging, and the ×4 un-averaging in its consumers,
are undocumented anywhere.** `Engine/Data/Shaders/Lighting/AGENTS.md` (and
`Engine/Data/Shaders/Water/AGENTS.md`). Lost specificity. Deleted from Lighting: "Also writes a
precomputed ambient texture holding `0.25 * (E+W+N+S)` of the tone-mapped per-direction values, so
consumers do one sample instead of three EWNS samples." Deleted from Water: "the smoke blend reuses
the same single ambient fetch via `BlendSmokePrecomputed` (multiplied by 4 to recover the
un-averaged sum)". Current Lighting text says only "writes the precomputed ambient target".
Evidence: `Engine/Data/Shaders/Lighting/LightCombine.comp:138` writes `dot(f4FinalR, vec4(0.25f))`
per channel with no comment; the two consumers un-average with a bare literal, also uncommented —
`Engine/Data/Shaders/Water/Water.frag:230` and `Engine/Data/Shaders/Terrain/Terrain.frag:158` both
call `BlendSmokePrecomputed(..., 4.0f * f3AmbientSum, ...)`; `ShaderFunctions.h:131-134`
(`ReadAmbientLighting`) does not mention the scale. Impact: a numeric contract spanning three shader
files with a bare `0.25` on one side and a bare `4.0` on the other and no owner. Changing the
combine pass to store an un-averaged or differently normalized sum will not surface the two `4.0f`
call sites, and the smoke blend on water and terrain goes off by 4× — a silent art-level regression.
**Action:** restore condensed onto the Lighting ambient bullet.

**S-2. Lighting delegates the lighting-area paragraph to a document that does not own it.**
`Engine/Data/Shaders/Lighting/AGENTS.md`. Broken reference. Current line 13: "The renderer's
Graphics documentation (`../../../Source/Graphics/AGENTS.md`) owns lighting-area headroom,
world-sized texels, zoom rescaling, and recreation behavior." Evidence:
`Engine/Source/Graphics/AGENTS.md` contains no occurrence of "headroom" or "world-sized"; its only
related line (`:19`) covers zoom rescaling but not headroom or the border-sampler contract. A
repository-wide search for "headroom" in `AGENTS.md` files returns exactly two hits: this broken
pointer, and `Engine/Source/Frame/Collections/AreaLights/AGENTS.md:10`, which assumes the reader
already knows what the factor is. Both facts remain live:
`Projects/BrokenEngineSandbox/Source/Graphics/Camera.h` defines `kfLightingHeadroomMultiplier`, and
`Engine/Source/Graphics/Managers/PipelineManager.cpp:446,466,517,526` bind the lighting and ambient
targets with `kSamplerBorder`, whose `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER` is created at
`Engine/Source/Graphics/Managers/TextureManager.cpp:352-353`. Impact: a reader following the pointer
finds nothing and concludes the headroom does not matter. The specific casualty is the "off-texture
reads as no light" guarantee — an editor changing the lighting sampler to clamp-to-edge (the more
common default) would smear the outermost lit texels across the whole off-texture margin during a
fast zoom-out. **Action:** rewrite line 13 to state the headroom and border-sampler contracts
locally, delegating only camera-height texel references and recreation to Graphics.

**S-3. The shared vertex/fragment interface rule lost the asymmetry that makes it actionable.**
`Engine/Data/Shaders/Quads/AGENTS.md`. Lost specificity. Deleted: "a frag input with no matching
vert output is a Vulkan error (`VUID-RuntimeSpirv-OpEntryPoint-08743`); an unconsumed vert output is
only informational (`WARNING-Shader-OutputNotConsumed`). Any location a shared frag reads must exist
on both `*VisibleArea.vert` files — e.g., the texture slot (location 7) is mirrored, with
`QuadsVisibleArea.vert` writing a dummy `0`. Locations 3 and 6 are unused/skipped in both
interfaces; respect the mirroring rule before reusing them." Current line 11 keeps only "Keep their
stage interfaces mirrored semantically; do not rely on one consumer's unused locations." Evidence:
`Engine/Data/Shaders/Quads/QuadsVisibleArea.vert:33` declares `location = 7` purely for the mirror
and `:38` writes the dummy `uiOutTextureSlot = 0u`; `QuadsAxisAlignedVisibleArea.vert:32` declares
the real one; four fragment shaders declare the matching unused input
(`Lighting/AreaLight.frag:35`, `Lighting/PointLight.frag:35`, `Smoke/Smoke.frag:20`,
`Wind/WindDeposit.frag:19`); both vertex shaders skip locations 3 and 6
(`QuadsVisibleArea.vert:27-33`, `QuadsAxisAlignedVisibleArea.vert:27-32`). Impact: the surviving
sentence says something must match but not which direction fails or how. Picking location 3 in one
vert only produces a pipeline-creation error whose VUID the document no longer names, and the
reader's likely first fix — deleting the "unused" fragment input — is the wrong one for the shared
frags. **Action:** restore condensed, including the one-directional failure and the reserved gap.

**S-4. Smoke's two spread passes are described as interchangeable, hiding which pass owns
extinction.** `Engine/Data/Shaders/Smoke/AGENTS.md`. Lost invariant. Deleted: "The passes use
distinct noise textures and scales. Pass B adds terrain-elevation decay …, edge-of-area fade, and
constant subtraction with threshold zeroing so faint smoke dies — load-bearing for the hierarchical
culling, since lingering nonzero texels would keep tiles occupied forever." Current line 3 says only
"two compute passes advect and diffuse it". Evidence: `SmokeSpreadOne.comp:60-65` calls
`SmokeSpread(...)` and stores the result with nothing else; `SmokeSpreadTwo.comp:68-97` calls the
same helper then adds terrain-elevation decay (`:69-74`), edge-of-area decay (`:78`), and the
half-precision quantization walk plus `kfSmokeZeroThreshold` zeroing (`:80-97`); different noise
scales at `SmokeSpreadOne.comp:63` versus `SmokeSpreadTwo.comp:67`. Impact: the document's own next
bullet states the exact-zero convergence requirement without saying which pass satisfies it. Adding
the decay/threshold block to pass A "for symmetry" doubles the per-frame extinction rate and
visibly shortens every plume; removing it from pass B during a refactor makes every touched tile
permanently active and silently converts the sparse dispatch back into a full-texture dispatch.
**Action:** restore condensed into the sparse-dispatch invariant list.

**S-5. The Wind document no longer says what the wind simulation computes.**
`Engine/Data/Shaders/Wind/AGENTS.md`. Over-compression. Deleted the entire "Simulation Kernel"
section — semi-Lagrangian advection with displacement clamped to 3 texels, world-position-anchored
swirl noise, simplified vorticity confinement, diffusion, frame-rate-independent decay, and the
magnitude regime where behavior constants are Low/High pairs blended by field magnitude — plus the
overview statement "Wind is purely visual and client-only — driven by wall-clock delta time, never
part of deterministic frame state." Evidence: all still true —
`Engine/Data/Shaders/Wind/WindSpreadCommon.h:9-32` implements the advection with the three-texel
clamp at `:26`, `:34-41` the world-anchored swirl noise, `:43-57` the vorticity confinement, `:59+`
diffusion; the magnitude regime is the file's dominant structural idea, with every behavior constant
a `mix(...Low, ...High, fMagFactor)` pair (`:18-22`, `:24`, `:35-37`, `:44`, `:60`). The current
document is 325 tokens against a 2,000 budget and describes only dispatch plumbing. Impact: without
the magnitude-regime statement, an editor adding a new wind behavior constant adds a single value
rather than the Low/High pair the rest of the kernel uses, breaking the laminar/turbulent split for
that term. The determinism sentence is partly carried by "client-only", but the reason — wall-clock
delta time — is what forbids ever feeding the field back into sim state. **Action:** restore
condensed as one paragraph after the opening.

**S-6. The hub's one-line EWNS definition asserts a component order the deposit shaders do not
use.** `Engine/Data/Shaders/AGENTS.md`. Introduced inaccuracy. Current line 18: "EWNS is the east,
west, north, and south components packed into one 4-float vector." **Attribution: this line is not
from the rewrite** — it was added later (by `88e6ab90`, `480e43d2`, or `887a6be5`); the rewrite's
contribution here was removing the hub's longer directional-deposit paragraph, which also never
pinned the component order. Evidence: consumers do use `(E, W, N, S)` —
`Engine/Data/Shaders/ShaderFunctions.h:147-153` weights component 0 by `max(+x, 0)`, 1 by
`max(-x, 0)`, 2 by `max(+y, 0)` (named `fWeightN`), 3 by `max(-y, 0)`; `WaterLighting` at
`ShaderFunctions.h:177-183` is identical; `DebugTexture.frag:106` agrees. Producers do not match on
the X axis: `Lighting/AreaLight.frag:69-72` stores `+dir.x` in the East slot and `-dir.y` in slot 2,
where `f2WorldDir` (`:60`) is the light-center-to-fragment offset; `LightingSpread.frag:127-129`
uses a third sign convention, `max(vec4(-dir.x, dir.x, -dir.y, dir.y), 0)`, with a comment mixing
world and pixel axes; `Objects/HexShieldLighting.frag:62` follows the AreaLight packing. Impact: the
sentence reads as authoritative, so an editor writing a new depositor packs by the plain reading and
gets the Y axis backwards relative to `AreaLight.frag`. **Action:** rewrite line 18 to state the
mapping in physical terms with `AreaLight.frag` as the reference packing. **Caveat carried forward,
do not upgrade:** the auditor offered the X/Y sign-convention disagreement between `AreaLight.frag`
and `LightingSpread.frag` explicitly as a **hypothesis for the owner, not an audit conclusion** — it
did not trace the full deposit → spread → combine → consume chain.

### 6.9 Sandbox core (`Projects/BrokenEngineSandbox/Source/{,Server}`) — 3 findings

**X-1. Game hub no longer says what BrokenEngineSandbox is.**
`Projects/BrokenEngineSandbox/Source/AGENTS.md`. Lost invariant (scope/rationale). Deleted: "Tech
demo that demonstrates and stress-tests engine features — not a game: no human player or win
condition. AI-driven spaceship fleets (flagship + wingmen) fight continuously spawning enemies over
the island ocean. All game code lives in the `game` namespace." Replaced by a purely structural
ownership sentence. Evidence: still true at `HEAD` —
`Projects/BrokenEngineSandbox/Source/Input/AGENTS.md:3` (client input produces only menu and camera
commands), `:5` (`game::Camera` reads `mCameraInput`, nothing feeds a player-controlled ship);
`Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.cpp:479-481` (spawn sets a
nav mode for every player; no human-input path exists). A repository-wide search of every
`AGENTS.md` for "tech demo", "no human player", "stress-test", or "demonstrat" returns nothing.
Impact: the hub is the first document a new editor reads; without it "client selection/settings" and
"the client player" read as a human-controlled player, and a future change could add privileged
human-player input handling, a win condition, or player-index privilege in the Frame — all
deliberately excluded. **Action:** restore condensed as the first sentence of `## Overview`.

**X-2. Server display's allocation-tracking contract deleted.**
`Projects/BrokenEngineSandbox/Source/Server/AGENTS.md`. Lost invariant. Deleted: "those call sites
also carry the broad `ScopedSuppressAllocationTracking` wraps for GDI/`InvalidateRect` allocations"
and "The text is also appended to a file-static `std::string` cache so the Copy button pushes the
last-painted text to the clipboard; that mutation is the one local
`ScopedSuppressAllocationTracking` in this directory." Current says only "Build transient text in
the thread-local workbuffer." Evidence: `Engine/Source/Main.cpp:422-424` (`// Heap: Win32
InvalidateRect may trigger internal GDI allocations` then the guard around
`ServerUpdateDisplayStats()` and the repaint block); `Engine/Source/Main.cpp:648-650` (same wrap
around `game::PaintServerDisplay(hWnd)` in `WM_PAINT`);
`Projects/BrokenEngineSandbox/Source/Server/ServerDisplay.cpp:430-432` (the single local guard for
the clipboard cache); `ServerDisplay.cpp:137` ("POD-only fold — no heap allocation, so it stays
inside the caller's ScopedSuppressAllocationTracking"). Impact: this directory runs entirely inside
the allocation-tracked server main loop; an editor adding a `std::string`, `std::vector`, or
`std::format` hits `DEBUG_BREAK()` and, not knowing the established pattern, either sprinkles new
local suppressions (defeating tracking) or widens the caller's wrap in engine `Main.cpp`.
**Action:** restore condensed into `## Ownership and Cadence`.

**X-3. `ServerDisplayContentChanged()` is stateful and must be called once per repaint decision.**
*(Post-rewrite drift, not caused by the rewrite.)*
`Projects/BrokenEngineSandbox/Source/Server/AGENTS.md`. Lost invariant. Current text mentions only
throttling and double-buffering; nothing about the content-hash skip, the visibility skip, or the
heartbeat. Evidence: `ServerDisplay.cpp:131` `bool ServerDisplayContentChanged()`; `:188-195` — it
compares against `suLastContentHash`, returns `false` when unchanged, and **mutates**
`suLastContentHash`/`sbHasLastContentHash` before returning `true`; `Engine/Source/Main.cpp:437-444`
— the only call site skips repaint when the window is minimized or hidden and otherwise repaints
only when `ServerDisplayContentChanged() || bHeartbeat`, with a 4-window (1 Hz at 32 Hz tick)
heartbeat so the free-running tick/timer text proves the server is still running; `ServerDisplay.cpp:175-186` shows the
hash deliberately excludes tick/time. Impact: an editor calling the function a second time (for a
log line, an agent query, or a second repaint condition) silently consumes the change and kills the
repaint, producing an apparently frozen server window that looks like a hang; and an editor adding a
new painted stat will not know it must be folded into the hash. **Action:** rewrite the throttle
bullet.

### 6.10 Sandbox frame and UI (`Projects/BrokenEngineSandbox/Source/{Frame,Ui}/**`) — 6 findings

**Y-1. `Frame::GetMissileTarget` mutates the frame and is order-dependent; no document says so.**
`Projects/BrokenEngineSandbox/Source/Frame/AGENTS.md`. Lost invariant. Deleted:
"`Frame::GetMissileTarget` is the shared enemy-target query used by missile/player homing — it
prefers under-subscribed targets (load-balances volleys) and registers a subscriber on success, so
it mutates the frame and call order within a tick is part of the deterministic stream". Evidence:
`Frame.cpp:458-514` ranks by `uiSubscribers < uiBestSubscribers` first and angle second, then calls
`TargetsPostRender::AddSubscriber(rFrame, uiTarget)` on success (`Frame.cpp:508-511`);
`Frame.h:200` declares it. No `AGENTS.md` states this now: `Collections/Targets/AGENTS.md:11`
mentions only alignment filtering, and `Collections/Missiles/AGENTS.md` says only that subscriptions
are validated and released. Impact: a query named `Get*` that silently writes to the frame and whose
result depends on how many callers ran before it this tick is exactly what someone reorders, hoists
out of a loop, memoizes, or parallelizes across missiles — any of which changes the CRC stream. The
load-balancing behavior is also a gameplay contract invisible from the signature. **Action:**
restore condensed into `## Invariants`.

**Y-2. Spaceships lost the "Spawn phase never creates spaceships" warning.**
`Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/AGENTS.md`. Lost invariant.
Deleted: "**Spawn phase misnomer**: The PostRender Spawn phase fires enemy blasters … and emits
staggered death explosions — it never creates spaceships. Creation is the `SpawnInfo` overload of
`Spawn`, called externally from `Frame.cpp` (chevron-group spawner) and `SpawnTransfer.cpp`."
Evidence: `Spaceships.cpp:477-586` — the phase `Spawn(Frame&, const FrameStaticData&)` only emits
staggered death explosions and fires blasters; `Spaceships.cpp:587` is the separate
`Spawn(Frame&, const SpawnInfo&)` creation overload; the chevron spawner is driven from
`Frame.cpp:398-412` on a fixed 0.5 s pulse. The sibling Blasters document still carries the
equivalent warning, so the omission is inconsistent within the same hub. Impact: two same-named
overloads where the phase hook does something unrelated to its name; an editor adding enemy-spawn
logic naturally puts it in the phase hook, where it runs per-tick per-entity inside the blaster-fire
loop, and an editor tracing "where do spaceships come from" finds the wrong function. **Action:**
restore condensed under `## Behavior Invariants`.

**Y-3. Spaceships lost the cross-phase field-ownership rule for the steering turn rate.**
`Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/AGENTS.md`. Lost invariant.
Deleted: "**Cross-phase field ownership**: Delta rotation and freeze time live in the Interpolate
struct (Interpolate Update and Render read them) but are decayed and written by PostRender Update."
Evidence: `Spaceships.h:64,68` — `pfDeltaRotations` is an Interpolate member and part of Interpolate
`SharedMembers()`; `SpaceshipsNavigation.cpp:168,171` — `SpaceshipsPostRender::AvoidTerrain` writes
`rCurrentInterpolate.pfDeltaRotations[i]`; `Spaceships.cpp:690-722` reads the previous Interpolate
value and stores the current one from PostRender-side code; `SpaceshipsRender.cpp:174` reads it at
render time. **Note:** the freeze-time half of the deleted sentence is now obsolete — no
`pfFreezeTimes` exists anywhere in the collection — so only the delta-rotation half should return.
Impact: a reader who does not know this one field is deliberately owned across the boundary either
"fixes" the apparent violation by moving it to PostRender (changing the CRC member set and the
render read path) or assumes other Interpolate fields may be written from PostRender. **Action:**
restore condensed, delta-rotation only.

**Y-4. Game collections hub lost the spawn/transfer vector-validation rule, and no document owns it
now.** `Projects/BrokenEngineSandbox/Source/Frame/Collections/AGENTS.md`. Lost invariant. Deleted:
"Spawn and Transfer sites validate position/velocity XMVECTORs via
`common::ValidateVector<IS_POSITION>()` to catch W-lane corruption at grid-boundary handoff."
Evidence: 26 `ValidateVector` call sites across the five game collections, for example
`Spaceships.cpp:592-594` (`ValidateVector<true>` on position, `<false>` on direction and velocity at
the spawn entry). Neither `Engine/Source/Frame/Collections/AGENTS.md` nor any game document mentions
`ValidateVector`; the engine hub covers only `LogDifferences` row bounds
(`Engine/Source/Frame/Collections/AGENTS.md:26`). Impact: the root `AGENTS.md` states the W-lane
convention but not that spawn and transfer are the enforcement points. Cross-cell handoff is
precisely where a W lane gets rebuilt from wire data; a new collection or spawn overload written
without these checks loses the tripwire that turns a W-lane mistake into an immediate assert instead
of a drifting position. **Action:** restore verbatim under `## Game-Specific Rules`.

**Y-5. HUD screen's network side effects are documented nowhere.**
`Projects/BrokenEngineSandbox/Source/Ui/Screens/AGENTS.md`. Lost invariant. Deleted:
"**HudScreen** - … Network-confirmed actions disable while pending; nav delay commits on drag
release, and focus changes refresh desired cell subscriptions." Evidence: `HudScreen.cpp:246`,
`:266`, `:334` — fleet/player focus controls call `gpClientSession->UpdateDesiredCoords(...)` with an
explicit `SubscriptionChangeReason`; `HudScreen.cpp:377-388` — the Nav Delay slider only sends
`SendFleetNavigationDelayRequest` inside `ImGui::IsItemDeactivatedAfterEdit()`, after `SetPending()`.
`Documents/UserInterfaceDesign.txt` is explicitly layout-only (lines 4-11, 18-24), so delegating to
it does not cover this. Impact: these are the only UI controls in the game that mutate network
subscription state and throttle a per-frame slider into one request. Someone reworking HUD panels —
a layout task by the authoritative document — can rebuild a focus button and silently drop the
subscription refresh (the client then renders a cell it never subscribed to), or convert the slider
to live-commit and emit 32 fleet requests a second. **Action:** restore condensed under
`## Shared Contracts`.

**Y-6. Lost the rule that the Graphics screen's time-of-day override must be seeded from the live
camera.** `Projects/BrokenEngineSandbox/Source/Ui/Screens/AGENTS.md`. Lost specificity. Deleted:
"**MainMenuScreen** - … Graphics seeds `engine::gSunAngleOverride` from the live camera, making Time
of Day main-menu-only; `Pch.h` toggles gate auto-launch/connect." Evidence:
`MainMenuScreen.cpp:130-134` — opening Graphics from the main menu sets
`engine::gSunAngleOverride.Set(gpCamera->RawSunAngle())`; `PauseMenuScreen.cpp:57` opens the same
screen with no seeding; `Graphics/Camera.cpp:313-325` — the camera returns the override for as long
as `meUiState == kGraphicsSettings`; `GraphicsMenuScreen.cpp:146` binds the slider to that global.
Impact: the seeding is a one-line coupling between two screens with no compile-time link, and the
in-game path already omits it (opening Graphics from Pause snaps the sun). Without the note, any new
entry point repeats the omission and nobody knows the existing asymmetry is real. **Action:**
restore condensed.

### 6.11 Meta, docs, and tools (`ThirdParty/`, `Documents/`, `Tools/`) — 4 findings

**T-1. `ThirdParty/AGENTS.md` delegates "deliberate exclusions" to a document that does not contain
them.** `ThirdParty/AGENTS.md`. Introduced inaccuracy. Current line 12:
"`Prebuilts/Platforms/VisualStudio2026/AGENTS.md` owns configuration, output naming, project
registration, and the deliberate client/server/DataPacker exclusions." Evidence:
`ThirdParty/Prebuilts/Platforms/VisualStudio2026/AGENTS.md` contains no exclusion content at all —
its sections are Overview, Build Configuration, Consumer Provisioning, Source Organization, and an
add-a-library checklist; the words "exclusion"/"exclude" do not appear anywhere (grep returns no
match). The exclusions that exist are visible only in source:
`ThirdParty/Prebuilts/Source/Engine/DirectXTK.cpp:9-12` (no `Keyboard.cpp`),
`DataPacker/Source/ExportJobs/Texture/Texture.cpp:386` (`params.m_use_bc7e = false;`), and
`ThirdParty/Prebuilts/Source/Engine/Stb.cpp:9-10` / `DataPacker/stb.cpp:14` (the split
`STB_IMAGE_*_IMPLEMENTATION` rule). Impact: worse than a plain deletion — the sentence tells a
reader the knowledge has a home, so a reader stops after one hop and concludes no exclusion policy
exists. This is the mechanism by which P1-6's content became unrecoverable. **Action:** rewrite line
12 to delegate only build configuration, output naming, and project registration, paired with the
P1-6 restore that gives the exclusions a real home in this file.

**T-2. A sibling document's back-reference into the deleted ThirdParty inventory now dangles.**
`ThirdParty/AGENTS.md`. Broken reference. Evidence:
`ThirdParty/Prebuilts/Platforms/VisualStudio2026/AGENTS.md:21` still reads "see the parent
`../../../AGENTS.md` for the **per-library consumer inventory** and the license policy (including
the `lz4/lib`-only caveat)", present and unmodified at `HEAD`; `280e89b7` did not touch that file.
The license policy and lz4 caveat do resolve in the current parent (`ThirdParty/AGENTS.md:16-31`);
the per-library consumer inventory does not exist there any more. Impact: the child document is the
one an editor reaches when registering a new compilation unit, and it explicitly promises the parent
will say which consumer each library serves (Engine vs DataPacker vs both). Two of the child's own
rules depend on that mapping: the `Engine`/`DataPacker`/`DataPacker\zlib` filter split, and the
`<ObjectFileName>` collision rule at `:27`. **Action:** restore a condensed consumer-split
paragraph, or — if the owner prefers no list at all — edit the child's line 21 to stop promising an
inventory, accepting that the consumer split then leaves documentation entirely.

**T-3. Landing-lock response schema rule ("responses omit `claimantPid`") deleted.**
`Tools/WorktreeCli/AGENTS.md`. Lost invariant. Deleted: "Landing-lock command responses expose lease
identity, timing, and validation state but omit private record provenance such as `claimantPid`;
process provenance never represents lease liveness." Only the second clause survives at
`Tools/WorktreeCli/AGENTS.md:9`. Evidence:
`Tools/WorktreeCli/LandingLockLifecycle.cpp:78-101` implements `LandingStatus` as a deliberate
allowlist — string fields `domain, logicalKey, owner, session, worktree, claimedAt, heartbeatAt,
expiresAt`, integer fields `schemaVersion, leaseDurationSeconds`, then `leaseState`. `claimantPid`
is written into the on-disk record (`Tools/ToolCommon/CoordinationStore.cpp:372`) and read for
validation (`:352-356`) but deliberately absent from the allowlist. Every landing-lock command emits
through that one function (`Tools/WorktreeCli/LandingLockCommands.cpp:125,150,175,204,308`). Impact:
the surviving half explains why checking whether the lease is still held is not PID-based but says nothing about what the response
may contain. An editor debugging a stuck lease sees a rich private record and a narrow response and
reads the allowlist as an oversight; adding `claimantPid` looks like a one-line improvement, and
publishes cross-session process provenance into a documented JSON schema, inviting callers to gate
on a PID. **Action:** restore condensed onto line 9.

**T-4. DirectXTK `Keyboard` rejection deleted from `ThirdParty/AGENTS.md`.** This was filed at P2 by
the G11 auditor and is **carried at P1 in section 5 (P1-6)**, merged with the G03 finding. It is
listed here only so the P2 count from the ThirdParty group reconciles; do not action it twice.

## 7. P3 findings

Forty-three optional fixes. All were verified with code evidence; none would cause an incorrect
change on its own.

| # | Path | Description | Recommended action |
|---|---|---|---|
| 1 | `Common/AGENTS.md`, `Common/Math/AGENTS.md` | Root W-invariant values restated inline in three documents plus a header comment, where the old text deferred by link; the explicit ownership pointers were deleted (`Common/Math/MathUtils.h:35-49` is the enforcing symbol) | Rewrite: drop the restatement, keep the pointer, and describe `ValidateVector<IS_POSITION>`'s enforcement role |
| 2 | `Common/Threading/AGENTS.md` | Worker thread priority not carried into the new document: every `PersistentWorker` runs at `THREAD_PRIORITY_TIME_CRITICAL` (`Common/Threading/PersistentWorker.cpp:9`) | Restore condensed — a `PersistentWorker` is not a host for background or I/O work |
| 3 | `Common/Threading/AGENTS.md` | `Dispatch` splits work `WorkerCount() + 1` ways because the calling thread runs a share (`Multithreading.h:39-41`, `:70-80`), and unconditionally dereferences `gpThreadLocal` (`:43-44`) | Add (new content, not a revert): under-provisioned per-partition scratch, and null-deref from a thread with no `ThreadLocal` |
| 4 | `Common/Log/AGENTS.md` | Line 7 says runtime levels default to `kInfo`; `Common/Log/Log.cpp:21-23` shows `kTemp` (index 1) defaults to `kVerbose` in every build. The clamp half is correct (`Engine/Source/Agent/AgentCommandsShared.cpp:163-168`) | Rewrite to add the `kTemp` exception |
| 5 | `DataPacker/Source/ExportJobs/Texture/AGENTS.md` | Asserts a sweep-diagnostics rule describing nothing: `RdoSweep.cpp` writes no files (`:52`, `:102`, `:143`, `:235` all log), and "current encoder settings" is true only of `RunRdoSweepValidate` (`:213-234`) | Rewrite to describe log-only reporting and no file mutation |
| 6 | `DataPacker/Source/ExportJobs/AGENTS.md` | Version convention lost the pointer to its enforcement site; `Common/DataFile.h:117,126,204,301,354,371,379` static_asserts name the owning job, and `ExportScene.h:26`/`ExportModel.h:21` fold payload sizes | Restore condensed |
| 7 | `DataPacker/Source/ExportJobs/AGENTS.md` | Legacy timestamp `.txt` metadata upgrade path now undocumented anywhere (`ExportJob.cpp:73-77`, `:136-149`, `:216`), hiding a compatibility shim from the retire-it decision | Restore condensed |
| 8 | `DataPacker/Source/ExportJobs/Island/AGENTS.md` | Beach-band subdivision stage omitted (`SubdivideBeachBand.cpp`, 406 lines; `BakeRoute.cpp:58-70`, `:72-75`), and automatic leaf pruning framed as a manual duty when `BakeRoute.cpp:184-192` runs it unconditionally | Rewrite |
| 9 | `Engine/Source/Memory/AGENTS.md`, `Engine/Source/AGENTS.md` | Allocation-discipline pointers now form a loop — each points at the other and neither states the usage rule (which lives in the always-loaded root document) | Rewrite one line in the hub |
| 10 | `Engine/Source/AGENTS.md` | Fullscreen precedence rule deleted (agent override beats `--windowed` beats persisted setting, none mutate the persisted value; `Main.cpp:36`, `:51-65`, `LaunchOptions.h:40-42`) and now appears in no `AGENTS.md`, though the harness `fullscreen` command depends on it | Restore condensed (fullscreen precedence only; the `SC_KEYMENU` suppression and terrain-before-Graphics ordering are well commented in source) |
| 11 | `Engine/Source/Audio/AGENTS.md` | Fade-band tuning surface and relative audible floor lost their cross-links (`StaticVoices.cpp:539-549`, `:650-657`; wrappers in `Engine/Source/Ui/SoundSettingsWrappersBase.cpp`, surfaced under Sound > Tweaks) | Restore condensed |
| 12 | `Engine/Source/Ui/Screens/TweaksScreen/AGENTS.md` | Two numbers flattened: the mask caps registration at **31**, not 32 (`TweaksScreenBase.h:8-9`, with a comment that 32 would be UB); sliders default to a 2.0 width multiplier and need 1.0 inside a multi-column table (`:95`) | Rewrite |
| 13 | `Engine/Source/Frame/Collections/AGENTS.md` | Type-registration asset-I/O side effect and register-once rule lost (`Collection.h:103-106`, `:126-144` schedule texture chunk load and lighting pre-blur on nonzero CRC) | Restore condensed |
| 14 | `Engine/Source/Frame/Collections/SmokeTrails/AGENTS.md` | Render-time unseeded RNG no longer identified as render-only (`SmokeTrailsRender.cpp:53`, consumed `:73`, `:75`, `:98`); looks like a determinism bug on sight | Restore condensed |
| 15 | `Engine/Source/Frame/Collections/AGENTS.md` | Mandatory allocation-tracking suppression for persistent collection storage weakened to a memory-location statement (`GameBase.cpp:505`, `Alignments.cpp:54` show the live requirement) | Restore condensed |
| 16 | `Engine/Source/Frame/Collections/AGENTS.md` and the Explosions, Puffs, PointLights, SmokeTrails leaves | Upward `See Also` links to the parent hub are missing. **Later drift (`887a6be5`/`88e6ab90`), not the rewrite** — the rewrite left them in place | Rewrite: re-add the parent links |
| 17 | `Engine/Source/Graphics/Render/AGENTS.md` | Day-cycle CPU/GPU ownership boundary deleted; ~250 lines of `GlobalUniforms.cpp` (`:24-51`, `:112-176`, `:279-301`, `:320-364`) are now undocumented at the architecture level | Restore condensed |
| 18 | `Engine/Source/Graphics/Managers/DeviceManager.AGENTS.md` | `OneShotCommandBuffer` non-overlap requirement dropped from both hub and leaf (`OneShotCommandBuffer.cpp:9-17`, a file-static `sbInUse` with `ASSERT(!sbInUse.exchange(true))`) | Restore condensed into the DeviceManager leaf rather than the hub |
| 19 | `Engine/Source/Graphics/Render/AGENTS.md` | Line 18 wrongly attributes "deposit grids" to shadow; lighting has a deposit grid (`LightingUniforms.cpp:125-140`, `:254-257`), shadow snaps to its own world-sized texel grid (`Render.h:31-55`, `GlobalUniforms.cpp:379`) | Rewrite |
| 20 | `Engine/Source/Graphics/AGENTS.md` | Island LRU grace clock lost its render-skip stall rationale (`Graphics.cpp:218-224`, `IslandTerrainResidency.cpp:206`, `Islands.cpp:304`, skip at `GameBase.cpp:595`); the "benign under render skip" half is in no comment | Restore condensed |
| 21 | `Engine/Source/Graphics/Render/AGENTS.md` | No file in the Graphics subtree states that render free-runs between sim ticks and sits outside the CRC. **Not a rewrite regression** — the pre-rewrite document did not state it either | Add one sentence |
| 22 | `Engine/Source/Graphics/Managers/TextureManager.AGENTS.md` | Blurred-lighting-texture lookup convention dropped (`TextureDescriptors.h:68` `kBlurSalt`, `:64`/`TextureDescriptors.cpp:700` `CrcToBlurredIndex`, `TextureManager.cpp:829-836` `ReblurAllLightingTextures`); the capacity invariant survives | Restore condensed |
| 23 | `Engine/Source/Graphics/Managers/TextureManager.AGENTS.md` | Per-frame adoption limits became "bounded"; the asymmetry (4 GPU-uploaded vs 1 fallback, `TextureManager.cpp:504`, `:521`, `:560`) is the non-obvious half | **No action** — reported only so the deletion is on the record |
| 24 | `Engine/Source/Graphics/Managers/ImGuiManager.AGENTS.md` | The repository-wide UI pixel-authoring convention thinned to a mechanism sentence (`ImGuiManager.h:110-116`, 25 call sites across Engine and Projects) | Restore condensed; note the game UI hub `Projects/BrokenEngineSandbox/Source/Ui/Screens/AGENTS.md` may be the better owner |
| 25 | `Engine/Source/Graphics/Managers/TextureUploadManager.AGENTS.md` | Upload pacing (one chunk per frame, driven by the render loop) no longer stated; "fixed staging budget" reads as a size cap, not a rate cap (`TextureUploadManager.cpp:229-231`, release at `:80-81`) | Restore condensed |
| 26 | `Engine/Source/Network/AGENTS.md` | ENet peer tuning dropped: throttle disabled and 1 MB socket buffers, on **both** sides (`Client.cpp:43-44`/`:163-164`, `Server.cpp:43-44`/`:157-158`). The rationale survives in code comments; the *paired* nature does not | Restore condensed |
| 27 | `Engine/Source/Network/Server/AGENTS.md` | Resend logging policy deleted: transitions only, per-slot cooldown (`ServerSend.cpp:245-258`, `kiResendLogCooldownTicks = 64`; field at `Server.h:43`) | Restore condensed |
| 28 | `Engine/Source/Network/Client/AGENTS.md` | Jitter and packet-loss metrics scale with the debug timescale (`TimeStep.h:37` `SimToWall`; `GameBase.cpp:130` and `ClientSession.cpp:190`; unscaled `kiJitterSafetyUs` at `ClientSessionRuntime.cpp:384`) | Restore condensed |
| 29 | `Engine/Data/Shaders/Water/AGENTS.md` | Source-side wave-normal dampener and zoom-out amplitude fade deleted (`WaterDisplacement.comp:127` applies the flat blend once in the pre-bake; band short-circuits at `:67`, `:95`) | Restore condensed (the dampener's "acts at the source" rule; the count/data pairing is owned upstream) |
| 30 | `Engine/Data/Shaders/Water/AGENTS.md` | Full-Jacobian requirement kept its rule but lost its failure mode and the named `Σ Q·ω·A → 1` boundary (`WaterDisplacement.comp:60-63`, `:124-126`; only a seven-word in-shader comment at `:60`) | Restore condensed |
| 31 | `Engine/Data/Shaders/Terrain/AGENTS.md` | MAX-blend rule no longer mentions the clear value it depends on (`RenderTargetTextures.cpp:98`, `:396` clear to `mfSeaFloorElevation`; rationale preserved in C++ comments at `PipelineManager.cpp:542`, `:304`) | Restore condensed |
| 32 | `Projects/BrokenEngineSandbox/Source/Server/AGENTS.md` | Server display specifics flattened: the `kiServerDisplayRepaintTicks` (8) throttle and its dominant-CPU-cost reason (`Main.cpp:426-427`), `mi_stats_*` under `ENABLE_CRT_DEBUG_HEAP` (`ServerDisplay.cpp:115-120`, `:183-186`), cached GDI object lifetime (`:43-59`, `:61`), and one-window-per-process file-static state (`:21`, `:32`, `:33`, `:36-37`) | Restore condensed (GDI lifetime and single-window only; the `mi_stats` compile-out needs no restore) |
| 33 | `Projects/BrokenEngineSandbox/Source/Graphics/AGENTS.md` | Camera's dead `Update(const Frame&)` overload and the update-entry contract lost (`Camera.cpp:65-68` is a one-line forward; only callers are `GameBase.cpp:568` and `Main.cpp:290`, both passing `FrameInterpolate`) | Restore condensed |
| 34 | `Projects/BrokenEngineSandbox/Source/Frame/AGENTS.md` | Game-owns-the-engine-queue-clear ordering contract lost (`Frame.cpp:428-443`, `:444-456`) | Restore condensed |
| 35 | `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/AGENTS.md` | Cross-collection type-registration ownership and the placeholder-audio note lost (`Blasters.cpp:25-30`, `BlastersUpdate.cpp:73-131`; commented-out `Sync` at `Blasters.cpp:138-146`, `BlastersUpdate.cpp:167-172`, with `puiSounds` still allocated at `:133`, `:207`) | Restore condensed |
| 36 | `Projects/BrokenEngineSandbox/Source/Ui/Screens/AGENTS.md` | Developer-tool gating of the game TweaksScreen undocumented (`TweaksScreen.cpp:12-15` — `kbDebugInput` compile switch plus `mbShowImGui` runtime toggle) | Restore condensed |
| 37 | `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/AGENTS.md` | Sizing single-source-of-truth stated for Spaceships but dropped for Players (`Players.h:24,27,38`; the parallel rule survives at `Spaceships/AGENTS.md:10`), breaking the "mirrored patterns stay parallel" directive | Restore condensed |
| 38 | `Tools/WorktreeCli/AGENTS.md` | Concrete 660-second build-lock wait flattened to "the standard lock wait" (`BuildCommand.cpp:26`, enforced one-directionally at `:64`) | Restore the number |
| 39 | `Tools/WorktreeCli/AGENTS.md` | Build-result exit-code contract for a failed retained log deleted (`BuildCommand.cpp:763-766`, `:768-772`, `:702`) | Restore condensed |
| 40 | `Tools/WorktreeCli/AGENTS.md` | Negative design constraint "do not add `--domain` or harness-key support" deleted; the underlying store is already domain-generic (`LandingLockLifecycle.cpp:81`, `Tools/ToolCommon/AGENTS.md:9`, hard rejection at `LandingLockCommands.cpp:80-95`) | Restore condensed — "it does not do X" reads as changeable, "do not make it do X" reads as a rule |
| 41 | `Documents/Plans/AGENTS.md` | Rationale for the required `## Out of scope` section deleted; it is the control `/scope-review` checks against. The one genuine rewrite-caused loss in this file — everything else it removed described the retired scored-queue system | Restore condensed |
| 42 | `Documents/Features/AGENTS.md` | `Revisit When` convention for deferred feature designs is undocumented. **Later drift (`45558faa`), not the rewrite — the rewrite kept it.** Four live documents follow it: `Future_CollectionVariants.txt:23`, `Future_EventMessageBus.txt:47`, `Future_SharedBehaviorTraits.txt:36`, `Future_TagBasedGenericIteration.txt:40` | Restore condensed |
| 43 | `AGENTS.md` (root) | The rewrite made three root edits despite the prompt excluding the root document: (a) a Claude-Code Fable→Opus fallback line, (b) the "Comment the non-obvious, not the mechanism" directive, (c) a `/repo-code-review` link fix. (b) survives verbatim at `HEAD`; (c) pre-empted a broken anchor (`SKILL.md` renamed §2c to "ASSERT behavior"); (a) is superseded — the role table now gates the Opus fallback on explicit user authorization | **No action** — (b) and (c) should stay. Recorded because two of the three were new policy authored on the model's own initiative inside the one document the task fenced off, and (a) silently altered which model reviews code before being replaced by an explicit-authorization gate |

## 8. What the rewrite got right

This section is not a courtesy. Several of these are strong enough that reverting them would make
the documentation worse.

**The Network group produced zero P1s.** It was judged the most disciplined part of the rewrite.
Deletions there were overwhelmingly inventories, call-chain narration, and duplicated parent rules,
and a large share of the apparent deletion was legitimate relocation to the new
`ClientSessionRuntime` owner created in the same commit.

**One rewritten invariant is strictly more accurate than the text it replaced.** The client
out-of-order full-state rule previously read "Out-of-order full state (before subscribe-accept)
adopts the coord". The rewrite changed it to say the full state claims the coord for the slot **only
when a matching `kSubscribing` placeholder still exists**, and that without that placeholder the
full state is a ghost and triggers an epoch-qualified unsubscribe. `ClientReceive.cpp:69-80` shows
that is what the code actually does. The pre-rewrite text was wrong.

**A stale allocator figure was corrected.** `Engine/Source/Memory/AGENTS.md` said "currently 8 GiB";
the rewrite changed it to 10 GiB, matching `Engine/Source/Memory/GlobalAllocator.cpp:87`
(`kiMimallocArenaReserveMb = 10 * 1024`).

**`Engine/Source/Memory` was not weakened.** Its only finding is a P3 about a documentation pointer
loop; the substance is intact and correct.

**`Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md` verifies clean against both
vcxprojs at `HEAD`.** Every load-bearing claim checks out: clang-tidy client Debug `true`
(`BrokenEngineSandbox.vcxproj:81`), Profile `false` (`:94`), Release `true` (`:113`); server `true`
in all three (`BrokenEngineSandboxServer.vcxproj:81`, `:93`, `:111`); the `ClangTidyChecks` analyzer
exclusion in each vcxproj (`:117`/`:115`); Release `CodeAnalysisRuleSet` pointing at
`BrokenEngineAnalysis.ruleset` (`:103`/`:101`); `CodeAnalysisTreatWarningsAsErrors` and
`RunCodeAnalysis` true in Release (`:110-111`/`:108-109`); `EnablePREfast` false in Profile
(`:199`/`:197`) and true in Release (`:266`/`:264`). It matches `/compile`'s two-path description
exactly.

**Several new leaf documents are accurate and are net gains.** `Projects/.../Source/Save/AGENTS.md`
(verified against `GameSaveLoad.cpp` version gate `:1118`/`:1180`, atomic write `:1120`, sorted coord
keys `:1137`, derived nav not persisted `:1143`, staged read with `AdoptGrid` applying counters and
clock only after full validation `:1260-1272`, failure clearing frames and fleet state `:1157-1163`)
carries strictly more information than the single hub bullet it replaced.
`Projects/.../Source/Agent/AGENTS.md` (shared-then-side-specific dispatch
`AgentCommands.cpp:171-198`; appdata-relative bare-filename validation rejecting NUL, separators,
`:`, `..`, and reserved device names, `AgentCommandsServer.cpp:30-73`) is accurate.
`Common/Threading/AGENTS.md` and `Common/Log/AGENTS.md` are accurate new leaves with only P3
omissions. `Engine/Source/Agent/AGENTS.md` extracts a 400-word hub bullet into its own document and
every claim checks out. `Projects/BrokenEngineSandbox/Data/AGENTS.md` and `Engine/Data/AGENTS.md`
are short, accurate delegation files whose every link resolves.

**Inventory and call-chain removal was correct throughout and should not be reverted.** Concrete
examples the auditors checked and deliberately declined to file: the network channel arithmetic
(`2 + slot*2`), whose formula and "never hardcode" intent survive as `NetworkManager` helpers plus a
channel-map comment at `NetworkManager.h:16-28`; the `CoordSubscriptionState` enum listing, whose
transitions are commented at `Client.h:46-53`; the server `## Build Configs` section, whose rule is
the authoritative property of
`Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/AGENTS.md:32-34`; the `ReconcileReplay`
four-file split and manager inventories; the entire scored plan-queue contract in
`Documents/Plans/AGENTS.md`, which describes a system removed by `45558faa`; the `Engine/Data/Shaders/Debug`
per-file inventory; the `TweaksScreen` overlay-screen list; and the `Projects/.../Source/Ui`
wrapper-file inventory. A stale claim was also correctly deleted: "`NetworkCursor.h` not aggregated
into `Engine.h`" is now false (`NetworkManager.h:3` includes it and `Engine.h:16` includes
`NetworkManager.h`), and its load-bearing half was kept.

**Several deletions are safely carried by unusually good source comments**, which is exactly the
trade `/update-claude-docs` asks for. The DataPacker group in particular is "the better class of
rewrite": almost every deleted specific survives as a comment at the exact site an editor would edit
(`ExportJob.cpp:213`, `ExportTexture.cpp:72`/`:122`/`:193`, `ExportIsland.cpp:68`/`:564`,
`AudioRepair.cpp:508`, the `Common/DataFile.h` static_asserts,
`Engine/Source/Frame/IslandTerrain.cpp:162`). Elsewhere: `Common/StackWalker.h:5-8` and `:17-19`
carry the DbgHelp serialization rule better than the deleted ThirdParty text did;
`ThirdParty/Prebuilts/Source/Engine/Stb.cpp:4-5` and `DataPacker/stb.cpp:6-7` carry the UTF-8
filename contract and the LNK4006 split rule; `Lz4.cpp:6-8` carries the `LZ4_SRC_INCLUDED` detail
verbatim; `ParticlesUpdate.comp:42-43` and `Terrain/Terrain.vert:71-73` still carry the glslang
whole-struct-copy investigation, so removing it from the shader documents was correct;
`Model/Model.frag:253-256` carries the `fPbrSun` linearity argument including the "would compound to
fPbrSun^3" reason; `Clear.frag:12` and `HdrResolve.frag:26-38` carry their deleted details verbatim;
`Render/MainUniforms.cpp:441-467`, `Render/WaterUniforms.cpp:54-173`, `Graphics.cpp:359-383`,
`Objects/Pipeline.h:109-111`, and `Objects/Texture.h:42` each carry a graphics deletion at the site
that matters.

**One correction the rewrite made is genuinely right and should be kept:** the SmokeTrails leaf's
"frame-rate and speed independent" claim was replaced with a caution not to strengthen an
implementation fact into a general guarantee. `SmokeTrailsRender.cpp:117` divides by segment length
and nothing more, so the original text overclaimed.

**Scope discipline was excellent and self-policed.** The session refused to touch the root document
as a matter of policy, refused to create files inside ThirdParty submodules after the user said so,
and its claim of "81 files, no root edit" holds up against the commit once the other sessions
squashed into `280e89b7` are separated out. It surfaced real contradictions rather than papering
over them, and it explicitly refused to let a good grade suppress a proven defect.

## 9. Process red flags

These are the mechanisms that would cause a repeat. They come from the transcript reconstruction of
the 2026-07-20 session, with the transcript auditor's own severities.

1. **[P1] Unbounded overshoot past the audit's own recommendation, with no floor and no reviewer.**
   The audit produced per-file, evidence-cited "remove X / keep Y" lists; implementers were free to
   cut anything not on the short `Keep/add` list, and did. `Engine/Source/File/AGENTS.md`: the audit
   said "37 tokens over budget… no missing durable rule found"; the implementer removed 1,366
   tokens, a 37× overshoot. Corpus-wide, 104,853 → 47,794 tokens against a total stated overage of
   about 8,500, so roughly 85% of the deletion had no budget justification at all. Mechanism: any
   invariant the auditor did not happen to enumerate in `Keep/add` was invisible to the implementer
   and to every downstream check.

2. **[P1] No verification pass compared deleted text against code.** Five verifiers ran. Every check
   was either mechanical (links, stubs, tokens, whitespace, scope count) or an accuracy check on the
   *surviving* text. The plan's verification list contained no minimality or retention criterion.
   Mechanism: a lost invariant produces no signal at all in this pipeline — it is not a broken link,
   not a failed budget, not an overstatement.

3. **[P1] The budget check is monotonic in the wrong direction, so over-compression scores as
   success.** The mechanical verifier reported "Budgets | Pass | Hub max 1,387; leaf max 1,849; zero
   failures" against ceilings of 4,000 and 2,000. Cutting a hub to 35% of its allowance registers
   identically to a well-judged trim. Mechanism: the only quantitative gate in the session rewards
   deletion without limit.

4. **[P1] Empirically-established negative knowledge was deleted under an "unprovable from current
   source" test.** The NVIDIA `inverse()` scoping sentence and the glslang SSBO-copy investigation
   record were both removed because they could not be re-derived by reading code. Mechanism:
   findings from debugging and disassembly are by construction not derivable from source, so this
   test guarantees their destruction. Both are load-bearing — they exist to stop a future engineer
   re-opening a settled question or refactoring the joint-matrix buffer. (The `inverse()` half is
   P1-5 in section 5; the glslang half turned out to be safely carried by in-shader comments, so it
   is not a document finding.)

5. **[P2] "Conciseness" was 15% of the grade that decided whether a file was rewritten at all.**
   Files scoring 92–96 (A) with "no missing durable rule found" were still flagged `Edit=Yes` and
   then cut 60–67%: `Engine/Source/Profile` 96 → −66%, `Engine/Source/File` 94 → −67%,
   `Engine/Source/AGENTS.md` 92 → −66%, `Engine/Source/Network` 95 → −56%. Mechanism: the rubric
   manufactured edit mandates for documents the audit had itself judged correct and complete.

6. **[P2] The user's approval gates never exposed a single deleted line.** The matrix table columns
   were `Document | Tokens | Score | Grade | Edit` — no before/after size, no sample removal, and the
   full report was delivered as a link to a local file. Approval came in 112 seconds; landing
   confirmation in 129 seconds. Mechanism: the one human check in the loop was structurally unable
   to see what it was authorising.

7. **[P2] Implementers largely paraphrased the audit reports rather than re-reading source.** The
   audit phase was genuinely rigorous — its auditors read 18 to 66 distinct source files each and
   cited real `file:line` evidence across 250+ files. The implementation phase did not:
   `implement_graphics_leaves` rewrote five documents in 13 shell commands, five of which were the
   token measurer; `implement_shader_leaves` rewrote all ten shader leaves, including cutting Water
   3,729 → 602 tokens, in 17 commands. Mechanism: the agent deciding *what to delete* had no
   independent view of what the code requires, only a summary of what the auditor thought worth
   keeping — and those `Keep/add` lists were never exhaustive inventories of what was in the file.

8. **[P2] Two implementers explicitly flagged compression risk and it was absorbed without a
   finding.** "Verify compressed Water guidance retains every decision-critical geometry, precision,
   specular-AA, and shadow boundary" and "Verify the game server leaf retains sufficient
   paused-service and load-reset distinctions after compression". The semantic reviewer named both
   areas in its coverage list and returned zero findings on either, with no visible command diffing
   old against new. Mechanism: a correct risk hand-off was closed by assertion.

9. **[P2] Algorithm rationale was explicitly targeted for removal and relocated to source comments
   never verified to exist.** Audit removals included "exact off-screen-distance derivation… tone-curve
   implementation detail" (Lighting), "the detailed `fPbrSun` algebra" (Model), and "exact
   energy-remap formulas, detailed sun/moon algebra… move local mathematical rationale to source
   comments" (Water). `/update-claude-docs` asks documents to keep algorithms and why a constraint
   exists. Mechanism: rationale deleted on the assumption of a comment nobody checked for. (In the
   Model case the comment did exist; in others it did not — see S-1 and S-5.)

10. **[P3] An external-verification route existed and deletion was chosen instead.** The R16_SFLOAT
    "spec-mandated" linear-filter claim was removed rather than routed to `/verify-external-claims`,
    with the alternative explicitly acknowledged in the synthesis. Low severity because the
    behavioural statement was retained; recorded because it is the same reflex as red flag 4.

11. **[P3] `fork_turns:"all"` on every audit and implementation delegate.** The current root
    `AGENTS.md` requires `fork_turns:"none"` with a self-contained brief. Every subagent inherited
    the full parent conversation, including the parent's own framing of the goal. Mechanism: the
    "fresh eyes" the review steps depend on were not fresh — the semantic reviewer read the diff
    already primed with the parent's belief that the deletions were approved and correct.

## 10. Preserving work done since the sweep

The rewrite is not the last thing that happened to these documents. Between `280e89b7` (2026-07-21)
and the `HEAD` this audit ran against, four commits deliberately improved them. Everything in
sections 5 through 7 has to be applied without undoing that work.

**The core rule: no restoration may be a verbatim revert.** Every restore must be re-expressed
against the *current* text of the file it lands in, and must carry forward the improvements already
there. A verifier who pastes pre-rewrite prose back in will silently undo a week of work.

### Commits that must be preserved

All four touched `AGENTS.md` files after the sweep:

| Commit | Date | `AGENTS.md` files | Nature |
| --- | --- | --- | --- |
| `11deeb50` "Plans with Codex" | 07-27 | 3 | workflow/process documentation |
| `1c6a6ce5` "Codex plans" | 07-28 | 12 | workflow, plus corrections to game-side navigation and save docs |
| `480e43d2` "Started removing manifest" | 07-29 | 9 | tracks the in-progress manifest removal |
| `887a6be5` "Fable back" | 07-30 | 39 | **the plain-language pass** — +99/−82 across 39 files |

### The plain-language pass specifically

`887a6be5` is the most recent and by far the most widespread of the four, and it is a deliberate
readability effort: it glosses jargon inline, in plain words, without changing meaning. Verified
examples:

- `Engine/Source/Graphics/AGENTS.md` — "opens the bindless write epoch" became "opens the bindless
  write epoch (a time window in which bindless descriptor writes are safe)".
- The term *epoch* is glossed repeatedly across the Frame collection documents as "the reuse counter
  — see `../AGENTS.md`".
- `Engine/Source/Input/AGENTS.md` — the heading "Input - Hardware Snapshot Publication" became
  "Input - Hardware Snapshot Sharing".
- The network documents gained plain-language expansions of protocol terms: "(full-state,
  coord-update, subscribe-accept)", "(sent once per subscription; carries NavData)", "(commit /
  clear-placeholder / epoch heal / reject-as-ghost)", and "(filling collection slots from received
  data — see `../../Frame/Collections/AGENTS.md`)".

That is the register the owner is moving these documents toward. The pre-rewrite prose this audit
quotes is the denser style that came *before* that pass, so restoring it byte-for-byte would move
the file backwards on the very axis the owner is actively improving.

### Derived requirements for every restoration

1. Restore the **invariant and its reason**, written fresh in the plain-language register
   `887a6be5` established — not the pre-rewrite wording.
2. **Gloss any jargon the restored text introduces**, in the same inline-parenthetical style, the
   first time that term appears in that document.
3. **Never revert a heading, gloss, or wording change** made by those four commits, even when
   restoring text into the same bullet or paragraph.
4. **Before editing any file, diff it against `280e89b7`** so you can see exactly what landed after
   the sweep and therefore what you must keep:
   `git diff 280e89b7..HEAD -- <path>`.
5. Where a P1 or P2 recommendation in this report quotes proposed replacement text, treat that text
   as **content to convey, not bytes to paste**. Re-render it in the current document's voice.
6. This report's recommended-text blocks were written against the files as they stood at audit time.
   **Re-check the live file first** — some may already have been partly addressed by the four
   commits above.

### Why this does not weaken the findings

The audit judged accuracy against `HEAD`, so a finding is about content that is *still* missing or
*still* wrong today. That part stands. What has moved is the surrounding prose those fixes land in,
and it moved in a direction the owner wants kept. Both facts are true at once: the knowledge gap is
real, and the paragraph you are about to edit is better than the one the rewrite left behind.

## 11. Recommended remediation, in priority order

**Order of work:**

1. **Read section 10 first.** It is a precondition, not a caveat: four commits since the sweep
   improved these documents, and a verbatim revert of any proposed text in this report would undo
   them. Nothing below is safe to apply without it.
2. **Then fix the eight P1s** (section 5). Each is a reachable wrong change with a stated failure
   mode; three of them end in a client/server desync. Start with P1-1, which is the only case where
   the current document actively asserts a falsehood that contradicts a live source comment.
3. **Then the P2s, by subsystem** (section 6). Taking them one subsystem at a time keeps each change
   small and lets one reviewer hold the whole context.
4. **Then, optionally, the P3s** (section 7). These are quality-of-life improvements. Two of them
   (#23 and #43) are explicitly marked "no action" and are recorded only so the deletion is on the
   record.

**How to restore.** Restores should be **condensed rewrites carrying the invariant and its reason**,
not verbatim reversions of the old prose. The rewrite was right that the old documents were verbose;
the problem was that it took the reason out with the verbosity. Every "Proposed text" in section 5
and every recommendation in section 6 is already written in that condensed form. The rule to apply
is the standard's own test, used in the direction it is usually forgotten: keep a sentence whose
absence would plausibly cause a worse future decision.

**Size is not a constraint.** Every one of the eleven auditors independently confirmed this for its
own group. Current sizes leave large headroom everywhere: `Common/AGENTS.md` sits at about 1,400
against a 4,000 hub target with roughly 2,600 tokens spare; the Engine core files run 530–1,242
against 2,000; the DataPacker hub is 1,178; the graphics manager references are 311–370 against
2,000; the shader hub is 839 against 4,000 and its largest leaf is 613 against 2,000. **Do not reject
any restore in this document on token-budget grounds.** If a restore ever does push a file over,
that is the signal to split the file, not to drop the invariant.

**Where a restore belongs in a source comment instead of an `AGENTS.md`.** Some knowledge is better
placed at the edit site than in directory memory. Specifically:

- **P1-3 (collision ordering)**: the tiebreak keys at `Engine/Source/Frame/Collision.cpp:391-395`
  have no comment explaining why they exist. A one-line comment there is worth adding alongside the
  document restore, because that is the line someone would "optimize".
- **P1-8 (`StreamingVoices::Clear`)**: there is no source comment on `Clear` recording the
  lock-free-callback dependency. Add one at `StreamingVoices.cpp:137` as well as the document line;
  the deferred-destruction contrast at `:128-130` shows the pattern.
- **P1-6 (`Keyboard`)**: `ThirdParty/Prebuilts/Source/Engine/DirectXTK.cpp:9-12` lists the compiled
  units with no note about the deliberate omission. A comment there would make the exclusion visible
  at the exact place someone would add `Keyboard.cpp`.
- **G-1 (descriptor routing flags)**: the four flags at `Engine/Source/Graphics/Objects/Pipeline.h:35-41`
  carry no comments while their neighbours do. Comment them, then the document only needs the rule.
- **G-5 (negative index sentinel)**: neither branch at
  `Engine/Source/Graphics/Objects/Pipeline.cpp:342` is commented; a comment there is cheaper than
  relying on the document alone.
- **S-1 (0.25 / ×4 ambient contract)**: the bare `0.25f` at `LightCombine.comp:138` and the bare
  `4.0f` at `Water.frag:230` and `Terrain.frag:158` should reference each other in comments, because
  the contract spans three files.

In each of those cases the document restore is still worth making — the comment helps whoever is
already at that line, the document helps whoever is deciding whether to go there.

## 12. How to verify this report

You are reading a snapshot. The repository has kept moving since the audit ran, and it will keep
moving. Before acting on anything here:

1. **Re-derive each P1 from its cited `file:line` against the current `HEAD`.** Open the named file,
   confirm the code still does what the finding says, and confirm the `AGENTS.md` text is still
   missing or still wrong. Do not restore text on this document's authority alone.
2. **Treat any finding whose evidence no longer resolves as expired.** If a line number has moved
   but the symbol is still there, re-anchor it. If the symbol is gone, the finding is dead — the
   code changed, and the deletion may now be correct.
3. **Check whether a later commit already fixed the item.** Several findings in this report were
   already attributed to later drift rather than to the rewrite (P3 items 16, 42; P2 items N-5, X-3;
   S-6's current wording). Run `git log --oneline <path>` for the file before assuming nothing has
   happened.
4. **Confirm the four post-sweep commits' contributions are still present in any file you edit.**
   Before committing a restore, run `git diff 280e89b7..HEAD -- <path>` and check that every change
   `11deeb50`, `1c6a6ce5`, `480e43d2`, and `887a6be5` made to that file is still in the text you are
   about to commit — the plain-language glosses and headings from `887a6be5` most of all. See
   section 10. A restore that reintroduces the invariant but drops a gloss has failed.
5. **Do not upgrade a caveated finding into a confident one.** The audit was careful to mark what it
   could not prove; that marking is part of the finding.

### Known-unreliable areas the auditors themselves flagged

- **`Common` LOG-format-spec rule (C-4).** The auditor verified that the rule was deleted, that no
  document now owns it, and that the codebase universally complies (zero `LOG(` call sites under
  `Engine/Source` or `Projects` use any `{:…}` spec). It **could not verify from repository source
  that a `{:…}` spec on a built-in MSVC formatter actually heap-allocates** — that behaviour lives in
  the STL, not in this repository. Confirm the mechanism (or route it through
  `/verify-external-claims`) before restoring the prohibition as an absolute.
- **Shader EWNS sign conventions (S-6).** The auditor offered the X/Y sign disagreement between
  `Lighting/AreaLight.frag:69-72` and `Lighting/LightingSpread.frag:127-129` explicitly as a
  **hypothesis for the owner, not an audit conclusion**, because it did not trace the full
  deposit → spread → combine → consume chain. The proposed replacement text is deliberately written
  as "the shape it needs", to be confirmed against `AreaLight.frag` before being pasted.
- **Transcript evidence limits (section 9).** Codex encrypts `spawn_agent` and `send_message`
  payloads, so **the literal dispatch prompts sent to the 34 subagents are unrecoverable**. The
  transcript auditor quoted the **approved plan text** instead, which each subagent carried verbatim
  because every one was forked with `fork_turns:"all"`. All parent-to-user messages, shell commands,
  and `apply_patch` payloads were plaintext and are quoted directly. Treat statements about *what an
  implementer was told* as inferred from the approved plan, not read from a dispatch message.
- **Contested items the original session left unresolved.** The `ShaderLayouts.h` project-membership
  contradiction (client-only membership despite a documented shared-header contract) was raised by
  four separate agents in that session and never fixed; it remains open and is outside this audit's
  scope. The R16_SFLOAT linear-filter statement was weakened by deletion rather than verified —
  check whether the surviving wording still tells a reader why linear sampling is safe.
- **Parent-to-child extraction accounting was never independently verified.** Ten documents were
  created as extractions from files being cut in the same change (`Common/{Log,Math,Threading}`,
  `DataPacker/Source/ExportJobs/{Island,Texture}`, `Engine/Source/Agent`,
  `Projects/.../Source/{Agent,Save}`, `Engine/Data`, `Projects/.../Data`). Content dropped in transit
  between parent and child would be invisible to every check that ran. The G01, G02, G03, and G09
  auditors each checked their own group's extractions and reported the results above, but this is
  the area where an undetected loss is most likely to remain.
- **Two rebase-conflict files.** `Projects/BrokenEngineSandbox/Source/Frame/AGENTS.md` and
  `Projects/.../Source/Network/Client/AGENTS.md` had their primary-side additions re-merged by hand
  during the original session and reviewed by that same session. Independent confirmation that both
  the audited text and the primary-side invariant survived is still worth doing.

### One severity divergence to be aware of

The DirectXTK `Keyboard` finding was graded **P1** by the G03 (EngineCore/Input) auditor and **P2**
by the G11 (ThirdParty) auditor. They were looking at different documents and different bad changes
— G03 at "an editor unifies keyboard input onto DirectXTK", G11 at "an editor adds `Keyboard.cpp` to
the unity unit". Both routes reach the same broken input path. This report carries it at P1 and
records both. That is a severity disagreement, not a factual contradiction: **no two group reports
disagreed on a fact.** The one genuine contradiction found anywhere is between a document and the
code it describes — P1-1, where
`Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/AGENTS.md:11` asserts the opposite of
the comment at `PlayersNavigation.cpp:304-312`. Per the repository's authority order, the code and
its comment win over the document there.

## 13. Open questions for the owner

These are decisions the audit could not make.

1. **Should `Engine/Source/Graphics/Managers/*.AGENTS.md` be exempt from leaf concision rules?**
   These eight documents are not directory memory. `/update-claude-docs` excludes `*.AGENTS.md` from
   audit discovery, and the only `CLAUDE.md` in that directory imports the hub, not these files.
   They exist specifically to hold detail that does not fit in the hub. Every one of them was
   already inside the 2,000-token leaf target before the rewrite (748–1,195 tokens) and was cut
   55–75% anyway; they now measure 311–370 tokens. Applying a concision target to documents whose
   whole purpose is to carry detail is what produced findings M-1 and M-2. The question is whether
   these get a different (or no) size target, and whether that exemption should be written into
   `/update-claude-docs` so a future pass does not repeat it.

2. **Should `/update-claude-docs` gain a retention check?** Nothing in the current process can
   detect that a concision pass deleted an invariant. The concrete proposal: for each sentence
   removed by a documentation change, require a decision of the form "still true and still
   decision-changing → keep, possibly condensed" or "obsolete / inventory / duplicated → remove",
   and require that an invariant leaving one document either land in another named document or in a
   named source comment, with the reviewer verifying that owner exists. Without something of this
   shape, red flags 1 through 3 in section 9 recur on the next concision pass by construction.

3. **How much of the removed material should come back at all?** This document proposes 8 required
   restores, 53 recommended ones, and 43 optional ones. The owner may reasonably decide that some
   P2s are not worth the words even though the knowledge is real. That is a judgement call about
   this repository's documentation appetite, and the audit deliberately did not make it.

4. **Should the two structural findings about knowledge with no home be resolved by document or by
   code comment?** Section 11 lists six places where a source comment would serve better than, or
   alongside, a document line. Whether to add those comments is a code change, not a documentation
   change, and would need its own classification.

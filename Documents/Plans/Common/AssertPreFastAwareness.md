# Make ASSERT / VERIFY_SUCCESS / CHECK_HRESULT PREfast-Aware

## Context

The `Release|x64` configuration of **every** first-party project enables MSVC static
analysis (PREfast) with warnings-as-errors:

- `DataPacker.vcxproj` — `EnablePREfast=true` (`:215`) + `TreatWarningAsError=true` (`:197`)
- `BrokenEngineSandbox.vcxproj` — `EnablePREfast=true` (`:247`) + `TreatWarningAsError=true` (`:229`)
- `BrokenEngineSandboxServer.vcxproj` — `EnablePREfast=true` (`:245`) + `TreatWarningAsError=true` (`:227`)

(The matching Debug configs set `EnablePREfast=false` / `TreatWarningAsError=false`, which is
why Debug builds are clean and the breakage only shows in Release.)

The validation macros in `Common/ErrorUtils.h` route failure through a plain function
`common::Assert(bool, ...)` / `common::CheckHresult(HRESULT, ...)` (`ErrorUtils.h:6-7`) that
PREfast does **not** model as terminating-on-false (it throws internally, but the signature is
not `[[noreturn]]` and PREfast can't see the throw). So after a guard like:

```cpp
ASSERT(gpThreadLocal != nullptr);
miPrior = gpThreadLocal->miLogTickCounter;   // PREfast: C6011 "Dereferencing NULL pointer"
```

PREfast still treats the pointer as possibly-null at the next dereference and emits **C6011**,
which `TreatWarningAsError` promotes to **C2220**, failing the Release compile. The first such
site (surfaced while compiling `Pch.cpp`) is `LogTickScope`'s ctor at
`Common/Threading/ThreadLocal.h:71`, but the defect is a *class*: every `ASSERT(ptr != nullptr)`
followed by a dereference across the codebase is a candidate.

The macros today (`ErrorUtils.h:11-14`):

```cpp
#define DEBUG_BREAK()  do { if constexpr (kbDebugBreak) { if (IsDebuggerPresent() == TRUE) { __debugbreak(); } } } while (false)
#define ASSERT(a)        do { if (!(a))            [[unlikely]] { DEBUG_BREAK(); common::Assert(a, #a); } } while (false);
#define CHECK_HRESULT(a) do { HRESULT hresultMacro = a; if (hresultMacro < 0) [[unlikely]] { DEBUG_BREAK(); common::CheckHresult(hresultMacro, #a); } } while (false);
#define VERIFY_SUCCESS(a) do { bool bReturnMacro = a; if (!bReturnMacro)      [[unlikely]] { DEBUG_BREAK(); common::Assert(bReturnMacro, #a); } } while (false);
```

The correct, surgical fix is to make the macros PREfast-aware with `_Analysis_assume_` — a SAL
annotation that emits **no runtime code** and only informs the analyzer of the post-condition the
macro guarantees. The idiom is already used in-tree (`Common/WindowsUtils.cpp:174`,
`_Analysis_assume_(pAttributeList != nullptr);`). Adding the post-condition once, inside the
macro, dissolves the entire C6011 class without touching any call site and without a blanket
`DisableSpecificWarnings` suppression (which would mask genuine null-deref bugs).

This was surfaced by the `WindowsUtils` resource-safety session: the WindowsUtils `.cpp` edit
forced a Common recompile under DataPacker Release PREfast, exposing the latent warning. It is
**not** caused by that change (PCH content was unchanged; failure occurred in `Pch.cpp` before
`WindowsUtils.cpp` was reached).

## Design

Add a no-codegen analyzer post-condition to each validation macro so PREfast knows the asserted
predicate holds on the fall-through (success) path.

### 1. `ASSERT(a)` (`ErrorUtils.h:12`) — effort: S

Append `_Analysis_assume_(a);` after the failure branch so the analyzer assumes `a` is true for
all subsequent code:

```cpp
#define ASSERT(a) do { if (!(a)) [[unlikely]] { DEBUG_BREAK(); common::Assert(a, #a); } _Analysis_assume_(a); } while (false);
```

> **Coordinate with `Common/ErrorUtils.md` (queued plan).** That plan already rewrites this macro
> to (among other things) capture the condition into a single local to fix the failure-path
> double-evaluation of `a`. The PREfast post-condition should be folded into that rewrite and
> phrased against the captured local — e.g. `bool bConditionMacro = (a); if (!bConditionMacro)
> [[unlikely]] { ... } _Analysis_assume_(bConditionMacro);` — so `a` is mentioned/evaluated once.
> If `ErrorUtils.md` lands first, add only the `_Analysis_assume_` line here; if this lands first,
> use the textual `_Analysis_assume_(a)` form and let `ErrorUtils.md` converge them. **Do not
> double-author the macro rewrite.**

### 2. `VERIFY_SUCCESS(a)` (`ErrorUtils.h:14`) — effort: S

It already captures the result into `bReturnMacro`; assume on that local:

```cpp
#define VERIFY_SUCCESS(a) do { bool bReturnMacro = a; if (!bReturnMacro) [[unlikely]] { DEBUG_BREAK(); common::Assert(bReturnMacro, #a); } _Analysis_assume_(bReturnMacro); } while (false);
```

### 3. `CHECK_HRESULT(a)` (`ErrorUtils.h:13`) — effort: S

Assume the success post-condition (`hresultMacro >= 0`, i.e. `SUCCEEDED`):

```cpp
#define CHECK_HRESULT(a) do { HRESULT hresultMacro = a; if (hresultMacro < 0) [[unlikely]] { DEBUG_BREAK(); common::CheckHresult(hresultMacro, #a); } _Analysis_assume_(hresultMacro >= 0); } while (false);
```

### 4. Validate against a Release PREfast build + triage residue — effort: M

Build `DataPacker` Release (then the two game projects' Release) with PREfast on and confirm the
C6011 class is gone. PREfast may surface *other*, unrelated warning classes (e.g. C6386, C6011 on
non-ASSERT-guarded paths, C26xxx) now that the build proceeds past `Pch.cpp`. For each remaining
warning, triage:

- **Genuine bug** → out of scope for this plan; spin a dedicated follow-up plan in
  `Documents/Plans/` (do not fix opportunistically here).
- **False positive** → add to the per-config `DisableSpecificWarnings` list (DataPacker Release
  already carries `28278;6385;6326;6340;6297;26115;26135;26408;26445` at `:217`) or annotate the
  specific site, matching the existing precedent — but only if it is demonstrably a false positive.

The acceptance bar for *this* plan is "the ASSERT-then-use C6011 class no longer fails the build,"
not "all of PREfast is green."

## Critical files

- `Common/ErrorUtils.h` — the three macro definitions (`:12-14`). All edits land here.
- `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj` — Release `EnablePREfast`/`TreatWarningAsError`/`DisableSpecificWarnings` (reference; only edited if step 4 triage adds a false-positive suppression).
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` / `BrokenEngineSandboxServer.vcxproj` — same Release PREfast settings (reference).

## Out of scope

- The other `ErrorUtils.h` macro fixes (failure-path double-eval, trailing `;` dangling-else,
  double `DEBUG_BREAK`) — owned by the queued `Common/ErrorUtils.md` plan. This plan adds **only**
  the PREfast post-condition and must co-implement / merge with that plan rather than re-author the
  macros.
- Making `common::Assert` / `common::CheckHresult` `[[noreturn]]` — they conditionally return by
  signature; the `_Analysis_assume_` post-condition is the correct, less invasive lever.
- Fixing any *genuine* bug PREfast flags once the build proceeds — each gets its own follow-up plan
  (step-4 triage records them).
- Turning PREfast off or relaxing `TreatWarningAsError` for Release — that masks the capability the
  Release `/analyze` pass provides.
- No Verification section. No unit tests.

## Acceptance criteria

- `Common/ErrorUtils.h`'s `ASSERT` / `VERIFY_SUCCESS` / `CHECK_HRESULT` each carry an
  `_Analysis_assume_` post-condition (no runtime codegen change — verified by identical behavior in
  Debug).
- A `DataPacker` Release build (`EnablePREfast=true`, `TreatWarningAsError=true`) no longer fails
  with C6011/C2220 on the `ASSERT(ptr != nullptr); ptr->...` pattern (concretely:
  `ThreadLocal.h:71` `LogTickScope` compiles clean).
- The client and server Release builds likewise clear the ASSERT-then-use C6011 class.
- Any remaining PREfast warnings are triaged (bug → new plan; false positive → documented
  suppression), with the residue recorded so the build state is honest.

## Notes

- `_Analysis_assume_` expands to nothing in a normal (non-`/analyze`) compile, so there is zero
  runtime/codegen impact and no determinism/CRC/network exposure — the change is invisible to the
  shipped binary and only steers the analyzer. This is why Risk is 1.
- The C6011 was latent, not new: Release PREfast had simply never been built to completion on this
  in-progress branch (or the analyzed `Pch.obj` was cached from a non-PREfast build). The macro fix
  is the root-cause remedy for the whole ASSERT-then-dereference class, versus patching individual
  sites one C6011 at a time.

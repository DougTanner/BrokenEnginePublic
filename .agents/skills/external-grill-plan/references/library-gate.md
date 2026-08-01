# Existing-Library Gate

Run this gate when a mature external library could plausibly replace substantial
custom work. Skip it for bug fixes, refactors, tuning, content, narrow glue, or
logic inseparable from internal engine types.

1. State the capability in one sentence and inspect `ThirdParty/` and
   `ThirdParty/Prebuilts/` for an existing dependency.
2. Identify at most three plausible commercial-friendly options. Treat every
   external fact as a separate stable-ID external-claim request for a delegated `locator`
   running `/verify-external-claims`:

   ```text
   Claim ID: EGP-EXT-###
   Proposition: <one fact that can be VERIFIED, REFUTED, or UNRESOLVED>
   Applicability: <project version, Windows/MSVC target, flags, or constraints>
   Dependent decision: <why this single fact changes the candidate choice>
   Candidate official source: <URL or exact upstream identifier, if known>
   ```

   License, current release/activity, Windows/MSVC support, C++ compatibility,
   determinism, and required feature support are distinct propositions. Never
   combine them into one verdict. Preserve stable IDs and exact verdicts; use
   only `VERIFIED` facts as established, and expose relevant `UNRESOLVED` facts.
3. Compare verified license, maturity, compatibility, and integration cost with
   the custom scope. Present two or three choices using the normal interaction
   contract: use an option, wrap it behind a thin adapter, or hand-roll for a
   specific evidenced reason.
4. If the user chooses a library, stop grilling the superseded custom design and
   return an integration-plan refinement covering vendoring and license notices,
   build/project/filter wiring, namespace/header isolation, adapter boundary,
   swapped call sites, exposed invariants, and acceptance checks. A library
   choice normally changes the work; never report it as "nothing to implement."
5. If the user chooses custom work, return the exact considered-library rejection
   rationale and continue with applicable checklist decisions.

A library or design pivot always returns to the manager for incorporation and a
fresh `/plan-audit`; it never changes the supplied plan text in place.

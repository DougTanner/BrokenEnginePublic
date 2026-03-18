# Tech Debt: Network Dead Code

Source: /external-tech-debt on Projects/BrokenEngineSandbox/Source/Network

## Changes

### Projects/BrokenEngineSandbox/Source/Network/ClientReconciler.cpp
- Line 179: Remove unused `[[maybe_unused]] const engine::Alignments& rAlignments` parameter from `ClientReconciler::Reconcile()`. Update the call site at line 145 (`Reconcile(*mpContext, mpContext->alignments)`) to remove the second argument. Also update the declaration in ClientReconciler.h [~5m]

### Projects/BrokenEngineSandbox/Source/Network/ReconcileReplay.cpp
- Line 198: Remove unused `[[maybe_unused]] ReconcileContext& rReconcileContext` parameter from `ReconcileInjectPendingFullState()`. Update call sites at lines 423 and 519 to remove the first argument. Also update the declaration in ReconcileReplay.h [~5m]

## Verification Notes
- Both parameters confirmed dead via code inspection. Line numbers and call sites verified correct.

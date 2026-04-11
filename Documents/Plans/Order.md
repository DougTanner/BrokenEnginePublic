# Plan Execution Order

Bugfixes, changes, and enhancements. Sorted by score (lowest = highest priority).

Score = Effort - Impact + Risks (lower = higher priority)

| # | Plan | Description | Effort | Impact | Risks | Score |
|---|------|-------------|--------|--------|-------|-------|
| 1 | [Network/TransferReconciliationBarrier.txt](Network/TransferReconciliationBarrier.txt) | Phase A diagnostic logs for Transfer-correlated CRC mismatches on kChina | 2 | 3 | 1 | 0 |
| 2 | [Network/TransferReconciliationBarrierFixB.txt](Network/TransferReconciliationBarrierFixB.txt) | Phase B per-tick barrier + client transfer sort (gated on Phase A logs) | 5 | 5 | 3 | 3 |


## Dependencies

- `Network/TransferReconciliationBarrierFixB.txt` depends on
  `Network/TransferReconciliationBarrier.txt` — Phase A logs must be captured
  and analyzed before Phase B is implemented.


Plans that must be executed in order due to shared files or stale line numbers:

## File Groups

Plans that touch the same files and should be done in a single session:

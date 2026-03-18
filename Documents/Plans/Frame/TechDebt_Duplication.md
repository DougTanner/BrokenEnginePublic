# Tech Debt: Duplication

Source: /external-tech-debt on Engine/Source/Frame (non-recursive)

## Changes

### Engine/Source/Frame/Alignments.cpp
- Extract the binary search comparator lambda duplicated in `AddAlignment` (line 23), `RemoveAlignment` (line 41), and `CanCollide` (line 55) into a file-static helper. All three use the identical lambda `[](const AlignmentPair& rPair, uint64_t uiKey) { return rPair.uiKey < uiKey; }` [~5m]

  Add before `AddAlignment`:
  ```cpp
  static auto AlignmentKeyLess = [](const AlignmentPair& rPair, uint64_t uiKey) { return rPair.uiKey < uiKey; };
  ```
  Then replace the three inline lambdas with `AlignmentKeyLess`.

## Verification Notes
All items verified against source. The lambda parameter `uiKey` shadows the outer local variable of the same name in each function, but since the lambda is captureless this is correct and the static version behaves identically.

# Refactor: Code Simplifications

Source: /external-refactor-clean on Common/

## Changes

### Common/ScopedLambda.h
- Replace `std::function<void()>` (line 10 and 33) with `std::move_only_function<void()>` for consistency with PersistentWorker.h and to avoid unnecessary heap allocation for type-erased captures
- Update constructor parameter type accordingly
- The try/catch in destructor can remain

### Common/Utils.cpp
- Simplify `PathToCppVariable` (lines 177-188): Replace 7 sequential `erase(remove(...))` calls with single-pass `std::erase_if` using a character set check:
  ```cpp
  std::string PathToCppVariable(std::string_view in)
  {
      std::string out(in);
      std::erase_if(out, [](char c) { return c == '\\' || c == '.' || c == ' ' || c == '[' || c == ']' || c == '-' || c == ','; });
      return out;
  }
  ```

### Common/MathUtils.h + Common/MathUtils.cpp
- Remove `InsideAreaVertices` declaration (MathUtils.h:23) and definition (MathUtils.cpp:52-58) — unused outside Common/ (only declared/defined, never called by any other file)
- Keep `AreaVertices` struct and `CalculateArea` — they ARE used by Blasters and Missiles game code
- Only `InsideAreaVertices` is dead (returns AreaVertices but is never called)

### Common/Utils.h + Common/Utils.cpp
- Remove `ToWstring` (Utils.h:185, Utils.cpp:146-149) — only called by CrashReport.cpp; move the trivial one-liner inline there
- (ToU32string — handled as dead code in TechDebt_DeadCode plan, zero callers)

## Verification Notes
- ScopedLambda is never copied (RAII guard pattern), so move-only semantics are correct
- CalculateArea and AreaVertices are used by game code and must be kept
- ToU32string has zero callers (not just CrashReport.cpp) — pure dead code

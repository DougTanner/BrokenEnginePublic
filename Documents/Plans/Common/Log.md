# Log.h / Log.cpp — Bounded Formatting, Prefix Correctness, and Hot-Path Cleanup

## Context

`common::Log` (Log.h:79-100) and its helper `LogPrefix` (Log.cpp:20-78) format every log line directly into a fixed `char[kiLogBufferSize]` buffer (`kiLogBufferSize = 32 * 1024`, ThreadLocal.h:50). The hot path is intentionally allocation-free: format into the fixed buffer, `memcpy` into lockless ring buffers, mutex only around emission. That design is sound, but the current implementation has several correctness/robustness gaps that are *not* defensive-input-validation — they are real memory-safety and output-correctness bugs:

1. The format/prefix writes use a bare advancing `char* pWrite` with no end pointer and the **unbounded** `std::format_to` overload, so a sufficiently long formatted line (or deep `ScopedLogIndent` nesting) silently overruns the fixed buffer — TLS/static corruption, UB in the diagnostic subsystem itself.
2. The thread-id prefix ladder hardcodes digit widths, ignores the `std::to_chars` result, and emits garbage bytes for thread ids ≥ 1000.
3. The ring-buffer copy recomputes `strlen` on every log call though the exact length is already known at the write site.
4. `LogIndent` dereferences `gpThreadLocal` unconditionally while `Log`/`LogPrefix` deliberately treat `gpThreadLocal == nullptr` as supported — an inconsistent invariant that null-derefs `ScopedLogIndent` on a no-`ThreadLocal` thread.

This plan does not touch the lockless ring-buffer/`Dump` concurrency model (by-design best-effort, mutex-only-on-emission) and does not touch the shared-static-buffer race (theme-owned, see Notes).

## Design

### C1 — Bound all writes into the fixed buffer (overrun fix) — effort 3
- The `std::format_to` into the per-thread/static buffer in `Log` — `Common/Log/Log.h:93` — is the unbounded overload and the raw `*(pWrite++)` newline/null writes at `Log.h:95-96` plus the `kTemp` `memcpy` at `Log.h:88-90` have no remaining-space check.
- Compute the writable end once in `Log`: reserve 2 bytes for `'\n'` + `'\0'` → `char* pEnd = pLogBuffer + kiLogBufferSize - 2;`.
- Replace `std::format_to(pWrite, format, parameters...)` with the bounded `std::format_to_n(pWrite, pEnd - pWrite, format, parameters...)` and take `.out` as the new `pWrite` (clamped).
- Guard the `kTemp` `memcpy` so it does not write past `pEnd` (clamp the copy length to the remaining space).
- Append `'\n'`/`'\0'` only within bounds; if the line filled the buffer, overwrite the final 2 bytes so the terminator invariant holds.
- Pass `pEnd` (or remaining space) into `LogPrefix` so prefix writes are likewise bounded (see M1/H4).

### M1 — Bound prefix indentation and `kTemp` against the buffer end — effort 1 (folds into C1)
- The indentation loop in `LogPrefix` — `Common/Log/Log.cpp:27-31` — writes `2 * miLogIndent` bytes with no bound; combined with `kTemp` (`Log.h:88-90`) it can exhaust the buffer before any format text.
- Using the `pEnd` introduced by C1, stop emitting indentation/prefix bytes once `pWrite >= pEnd`. No separate mechanism — this is the same clamp.

### H4 — Correct thread-id prefix via the `to_chars` result pointer — effort 2
- The thread-id ladder in `LogPrefix` — `Common/Log/Log.cpp:35-49` — special-cases `>= 100` / `>= 10` / else, ignores the `std::to_chars` return, and advances `pWrite` by a hardcoded count (`++pWrite` triplet at `Log.cpp:38`). For ids ≥ 1000 the `>= 100` branch hands `to_chars` only a 3-char range, `to_chars` fails (`errc::value_too_large`, writes nothing) yet `pWrite` still advances 3 bytes — emitting 3 bytes of stale buffer content as the "thread id."
- Replace the entire ladder with the pattern the tick branch already uses correctly (`Log.cpp:64-65`): `auto r = std::to_chars(pWrite, pEnd, gpThreadLocal->miThreadId.value()); pWrite = r.ptr;`. Correct for any width, advances by exactly the digits written, and removes the magic triplets. (Style sub-points L2/L3 from the report — inconsistent bounding style and the char-by-char `"[Tick: "` spelling — fold into this and may be left as-is if out of appetite.)

### H3 — Pass the known line length instead of re-`strlen` on the hot path — effort 2
- `LogWriteRingBuffers` / `copyToLine` — `Common/Log/Log.cpp:92-104`, called from `Log.h:98` — calls `strlen(pLogBuffer)` once per destination buffer (twice per log call) although `Log` already knows the end: `pWrite - pLogBuffer` after the `'\0'`.
- Change the signature to `LogWriteRingBuffers(const char* pLogBuffer, int64_t iLength, LogCategory eCategory)` (declaration `Log.h:75`), pass `iLength = pWrite - pLogBuffer` (includes the `'\0'`) from `Log` (`Log.h:98`), and `memcpy` exactly that many bytes — removing both `strlen` scans per call.

### M3 — Make `LogIndent` consistent with the null-`gpThreadLocal` contract — effort 1
- `LogIndent` — `Common/Log/Log.cpp:12-18` — does `gpThreadLocal->miLogIndent += iIndent;` with no null guard, while `LogPrefix` (`Log.cpp:24`) and `Log` (`Log.h:83`) treat `gpThreadLocal == nullptr` as supported. A `ScopedLogIndent` on a no-`ThreadLocal` thread null-derefs.
- Since `Log`/`LogPrefix` deliberately handle null, guard `LogIndent` with `if (gpThreadLocal != nullptr)`. Note the same latent unconditional deref exists in `LogTickScope` (ThreadLocal.h:37,45); flag it but do not expand scope here unless trivially co-located — if not fixed, surface as a follow-up.

## Critical files
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Log\Log.h` — `Log` template (lines 79-100), `LogWriteRingBuffers` decl (line 75).
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Log\Log.cpp` — `LogPrefix` (20-78), `LogWriteRingBuffers`/`copyToLine` (92-104), `LogIndent` (12-18).
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Threading\ThreadLocal.h` — `kiLogBufferSize` (50), `mpLogBuffer` (24); `LogTickScope` null-deref note (37,45).

## Out of scope
- The lockless ring-buffer write-vs-`Dump` race (`AcquireLine`/`Dump`, Log.h:26-60; `copyToLine` memcpy without happens-before): by-design best-effort crash-path snapshot, mutex-only-on-emission per Common/CLAUDE.md. Not addressed here.
- The shared function-static `spcLogBuffer` data race when `gpThreadLocal == nullptr` on multiple threads (Log.h:82-83) — THEME-OWNED (see Notes).
- `printf("%s", ...)` boundedness (Log.cpp:86-89), `giMyOutputDebugString` global suppression semantics (Log.cpp:6,80-90), unbounded-counter tidiness in `AcquireLine` (Log.h:26-35), global-scope `using` in the header (Log.h:128-130), and the lack of a compile-time guard for the no-float-format `LOG` rule — all documentation/cosmetic/by-design, dropped.
- No defensive validation of caller-supplied format strings/args (project rule: assume parameters valid). The C1 clamp is correctness of the engine's own write bookkeeping, not input validation.

## Acceptance criteria
- No write in `Log` or `LogPrefix` can advance past `pLogBuffer + kiLogBufferSize`; an over-long line or deep indent is truncated with a valid terminating `'\0'` (and trailing `'\n'` if room) — verified by a long-message and deep-`ScopedLogIndent` exercise.
- Thread ids of any magnitude (including ≥ 1000) print their exact decimal digits with no stale/garbage bytes; `pWrite` advances by exactly the digit count via the `to_chars` result pointer.
- `LogWriteRingBuffers` performs zero `strlen` calls; the line length flows from `Log` (`pWrite - pLogBuffer`).
- `LogIndent` no longer dereferences a null `gpThreadLocal`; `ScopedLogIndent` is safe on a thread without a `ThreadLocal`.
- Hot path remains allocation-free; ring-buffer copies still `memcpy` into the fixed line storage.

## Notes
- The shared function-static `spcLogBuffer` data race (`Log.h:82` — single static buffer shared by all threads that log with `gpThreadLocal == nullptr`) is **owned by the theme plan `ThreadSafety_SharedStaticBuffers.md`** and is intentionally excluded here. The report's H1/L6 both describe this same race; they are deferred to that theme plan, not addressed by this plan.
- C1's `pEnd` is a shared prerequisite for M1 and H4 — implement C1 first, then thread `pEnd` through `LogPrefix`.

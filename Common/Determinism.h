#pragma once

// Determinism.h relies on DirectXMath types (XMVECTOR / XMFLOAT2-4 / XM_CALLCONV); it must be included after the
// DirectXMath headers (ExternalHeaders.h does this). Enforce that ordering invariant at compile time.
#if !defined(DIRECTX_MATH_VERSION)
	#error "Include DirectXMath before Determinism.h"
#endif

// Deterministic exact (non-epsilon) IEEE equality for DirectXMath types. This is value equality, not
// bitwise identity: -0.0f equals +0.0f and NaNs never compare equal. Consumers requiring byte identity
// use byte comparison (for example, LogDifference). Included from ExternalHeaders.h immediately after
// the DirectXMath includes so these operators are globally visible wherever the types are used. The
// SSE4-only build knob, the PI subdivisions, kfEpsilon, and XmIsNan/XmIsInf remain in ExternalHeaders.h.
inline bool XM_CALLCONV operator==(FXMVECTOR rOne, FXMVECTOR rTwo)
{
	return XMVector4Equal(rOne, rTwo);
}

inline bool operator==(const XMFLOAT2& rOne, const XMFLOAT2& rTwo)
{
	return rOne.x == rTwo.x && rOne.y == rTwo.y;
}

inline bool operator==(const XMFLOAT3& rOne, const XMFLOAT3& rTwo)
{
	return rOne.x == rTwo.x && rOne.y == rTwo.y && rOne.z == rTwo.z;
}

inline bool operator==(const XMFLOAT4& rOne, const XMFLOAT4& rTwo)
{
	return rOne.x == rTwo.x && rOne.y == rTwo.y && rOne.z == rTwo.z && rOne.w == rTwo.w;
}

namespace common
{

// Per-thread floating-point determinism setup (flush denormals, round-to-nearest). Called by ThreadLocal ctor.
void ConfigureThreadFloatingPoint();

// Installs the SEH translator, invalid-parameter handler, vectored exception handler, and terminate handler.
// Promotes structured faults into C++ exceptions and logs a stack walk before DEBUG_BREAK(); stack overflow logs a
// fixed error and skips the stack walk. Called by ThreadLocal ctor.
void SetupExceptionHandling();

// DbgHelp (dbghelp.dll) is documented single-threaded per process — every stack-walk driver must serialize on
// this mutex. Acquired try_to_lock by FilteredStackWalker::ShowCallstack (StackWalker.h); recursive so a fault
// inside DbgHelp during a walk re-enters instead of deadlocking.
extern std::recursive_mutex gDbgHelpMutex;

// Expected-throw region depth for this thread. While nonzero, the vectored exception handler treats C++ throws
// (0xE06D7363) as an expected, designed error path — it skips the stack walk / DEBUG_BREAK() and lets the throw
// propagate to its catch. Other exception codes (access violation, heap corruption, etc.) are unaffected: they
// still get the full callstack even inside such a region. Increment/decrement only via ScopedExpectedThrows.
extern thread_local int64_t giExpectedThrowDepth;

// RAII guard entering an expected-throw region on the current thread — ctor increments giExpectedThrowDepth, dtor
// decrements. Non-copyable/non-movable (scope-bound; a moved-out copy would double-decrement).
struct ScopedExpectedThrows
{
	ScopedExpectedThrows()
	{
		++giExpectedThrowDepth;
	}

	~ScopedExpectedThrows()
	{
		--giExpectedThrowDepth;
	}

	ScopedExpectedThrows(const ScopedExpectedThrows&) = delete;
	ScopedExpectedThrows& operator=(const ScopedExpectedThrows&) = delete;
	ScopedExpectedThrows(ScopedExpectedThrows&&) = delete;
	ScopedExpectedThrows& operator=(ScopedExpectedThrows&&) = delete;
};

} // namespace common

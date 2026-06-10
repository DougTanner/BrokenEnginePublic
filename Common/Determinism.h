#pragma once

// Determinism.h relies on DirectXMath types (XMVECTOR / XMFLOAT2-4 / XM_CALLCONV); it must be included after the
// DirectXMath headers (ExternalHeaders.h does this). Enforce that ordering invariant at compile time.
#if !defined(DIRECTX_MATH_VERSION)
	#error "Include DirectXMath before Determinism.h"
#endif

// Deterministic, bitwise (non-epsilon) equality for DirectXMath types — central to CRC-based
// client/server reconciliation. Included from ExternalHeaders.h immediately after the DirectXMath
// includes so these operators are globally visible wherever the types are used. The SSE4-only build
// knob, the PI subdivisions, kfEpsilon, and XmIsNan/XmIsInf remain in ExternalHeaders.h.
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
// Promotes structured faults into C++ exceptions and logs a stack walk before DEBUG_BREAK(). Called by ThreadLocal ctor.
void SetupExceptionHandling();

// DbgHelp (dbghelp.dll) is documented single-threaded per process — every stack-walk driver must serialize on
// this mutex. Acquired try_to_lock by FilteredStackWalker::ShowCallstack (StackWalker.h); recursive so a fault
// inside DbgHelp during a walk re-enters instead of deadlocking.
extern std::recursive_mutex gDbgHelpMutex;

} // namespace common

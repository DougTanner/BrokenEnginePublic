#pragma once

// Deterministic, bitwise (non-epsilon) equality for DirectXMath types — central to CRC-based
// client/server reconciliation. Included from ExternalHeaders.h immediately after the DirectXMath
// includes so these operators are globally visible wherever the types are used. The SSE4-only build
// knob, the PI subdivisions, kfEpsilon, and XMISNAN/XMISINF remain in ExternalHeaders.h.
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

} // namespace common

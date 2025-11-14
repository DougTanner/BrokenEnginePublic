#pragma once

namespace common
{

struct RandomEngine;

struct AreaVertices
{
	XMVECTOR vecTopLeft {};
	XMVECTOR vecTopRight {};
	XMVECTOR vecBottomLeft {};
	XMVECTOR vecBottomRight {};

	bool operator==(const AreaVertices& rOther) const = default;
};

XMVECTOR XM_CALLCONV ColorToVector(uint32_t uiColor);
// Projects a 3D position to a specific height plane using plane-line intersection
// vecPosition: The 3D position to project
// vecEyePosition: The eye/camera position defining the projection ray
// fBaseHeight: The target Z height to project onto
// Returns the projected position at the specified base height
XMVECTOR XM_CALLCONV ToBaseHeight(FXMVECTOR vecPosition, FXMVECTOR vecEyePosition, float fBaseHeight);
// Calculates rotation angle from a 2D position vector using arctangent
// vecPosition: The position vector (X-Y components used)
// Returns angle in radians from the X-Y coordinates
float RotationFromPosition(FXMVECTOR vecPosition);
XMVECTOR XM_CALLCONV QuaternionFromDirection(FXMVECTOR vecDirection, FXMVECTOR vecOriginNormal, FXMVECTOR vecUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
// Generates a rectangular area (quad) from a center position, direction, and dimensions
// vecPosition: Center position of the area
// vecDirection: Forward direction vector (normalized)
// fForward: Distance forward from center
// fBack: Distance backward from center
// fWidth: Total width of the area (half-width applied on each side)
// Returns AreaVertices structure containing the four corner vertices
AreaVertices XM_CALLCONV CalculateArea(FXMVECTOR vecPosition, FXMVECTOR vecDirection, float fForward, float fBack, float fWidth);
// Tests if a point is inside a quad defined by AreaVertices
// vecPosition: The 3D point to test
// rAreaVertices: The quad defined by four corner vertices
// Returns true if the point is inside the quad (using two triangle intersection tests)
bool XM_CALLCONV InsideAreaVertices(FXMVECTOR vecPosition, const AreaVertices& rAreaVertices);
// Rotates a direction vector towards a target direction by a percentage of the angle between them
// vecDirection: The current direction vector (normalized)
// vecTowards: The target direction vector (normalized)
// fPercent: Percentage of rotation to apply (0.0 = no rotation, 1.0 = full rotation)
// Returns the rotated direction vector, used for smooth rotation interpolation
XMVECTOR XM_CALLCONV RotateTowardsPercent(FXMVECTOR vecDirection, FXMVECTOR vecTowards, float fPercent);

// Creates a rotation matrix from a direction vector by wrapping QuaternionFromDirection
// vecDirection: The direction vector to convert to a rotation matrix
// vecOriginNormal: The normal vector of the origin plane
// vecUp: The up vector for the rotation (default: Z-up)
// Returns rotation matrix for object orientation
inline XMMATRIX XM_CALLCONV RotationMatrixFromDirection(FXMVECTOR vecDirection, FXMVECTOR vecOriginNormal, FXMVECTOR vecUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f))
{
	return XMMatrixRotationQuaternion(QuaternionFromDirection(vecDirection, vecOriginNormal, vecUp));
}

// Calculates Euclidean distance between two 3D vectors
// vecOne: The first vector position
// vecTwo: The second vector position
// Returns the distance between the two points using XMVector3Length
inline float XM_CALLCONV Distance(FXMVECTOR vecOne, FXMVECTOR vecTwo)
{
	return XMVectorGetX(XMVector3Length(XMVectorSubtract(vecTwo, vecOne)));
}

// Returns the normalized direction vector from one point to another
// vecFrom: The starting position
// vecTo: The target position
// Returns normalized direction vector, or zero vector if points are identical (within epsilon)
inline XMVECTOR XM_CALLCONV DirectionTo(FXMVECTOR vecFrom, FXMVECTOR vecTo)
{
	if (XMVectorGetX(XMVectorNearEqual(vecFrom, vecTo, g_XMEpsilon)) != 0.0f) [[unlikely]]
	{
		return XMVectorZero();
	}

	return XMVector3Normalize(XMVectorSubtract(vecTo, vecFrom));
}

// Rounds up an integer to the nearest multiple (ceiling)
// iToRound: The value to round
// iMultiple: The multiple to round to
// Returns the rounded value, used for memory alignment and buffer sizing
template<std::integral T>
constexpr inline T RoundUp(T iToRound, T iMultiple)
{
	return ((iToRound + iMultiple - 1) / iMultiple) * iMultiple;
}

// Rounds down an integer to the nearest multiple (floor)
// iToRound: The value to round
// iMultiple: The multiple to round to
// Returns the rounded value
template<std::integral T>
constexpr inline T RoundDown(T iToRound, T iMultiple)
{
	return (iToRound / iMultiple) * iMultiple;
}

// Rounds down a floating-point number to the nearest multiple using inverse multiplication and floor
// fToRound: The floating-point value to round
// fMultiple: The multiple to round to
// Returns the rounded value
template<std::floating_point T>
constexpr inline T RoundDown(T fToRound, T fMultiple)
{
	float fInv = 1.0f / fMultiple;
	return std::floor(fToRound * fInv) / fInv;
}

// Converts a normalized float (0.0-1.0) to an unsigned integer representation
// fValue: Normalized float value (asserted to be in range [0.0, 1.0])
// Returns unsigned integer (e.g., 0-255 for uint8_t, 0-65535 for uint16_t)
template <typename T>
inline T FloatToUnorm(float fValue)
{
	ASSERT(fValue >= 0.0f && fValue <= 1.0f);
	return static_cast<T>(static_cast<float>(std::numeric_limits<T>::max()) * fValue);
}

// Converts an unsigned integer to a normalized float (0.0-1.0)
// uiValue: Unsigned integer value
// Returns normalized float, inverse operation of FloatToUnorm
template <typename T>
inline float UnormToFloat(T uiValue)
{
	return static_cast<float>(uiValue) / static_cast<float>(std::numeric_limits<T>::max());
}

// Converts a gamma-corrected value to linear space using gamma 2.2
// fGamma: Gamma-corrected color value
// Returns linear space value (standard gamma correction inverse)
inline float FromGamma(float fGamma)
{
	return std::pow(std::max(0.0f, fGamma), 1.0f / 2.2f);
}

} // namespace common

#pragma once

namespace game
{

inline constexpr float kfAiEdgeCrossCooldown = 20.0f;

struct AiSteeringResult
{
	XMVECTOR vecAiDirection;
	float fAiEdgeCrossCooldown;
	int8_t iAiEdgeCrossTarget;
};

AiSteeringResult XM_CALLCONV ComputeAiSteering(FXMVECTOR vecPosition, FXMVECTOR vecCurrentDirection, FXMVECTOR vecArea, CXMVECTOR vecFrameCenter, float fDeltaTime, float fAiEdgeCrossCooldown, int8_t iAiEdgeCrossTarget, bool bAlternateContour);

float XM_CALLCONV ComputeTerrainAvoidance(FXMVECTOR vecPosition, FXMVECTOR vecDirection, float fCurrentDeltaRotation);

} // namespace game

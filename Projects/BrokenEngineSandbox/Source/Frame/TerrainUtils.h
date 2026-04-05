#pragma once

namespace game
{

struct AiSteeringResult
{
	XMVECTOR vecAiDirection;
};

AiSteeringResult XM_CALLCONV ComputeAiSteering(FXMVECTOR vecPosition, FXMVECTOR vecCurrentDirection, FXMVECTOR vecFrameCenter, float fDeltaTime, bool bAlternateContour);

float XM_CALLCONV ComputeTerrainAvoidance(FXMVECTOR vecPosition, FXMVECTOR vecDirection, float fCurrentDeltaRotation);

} // namespace game

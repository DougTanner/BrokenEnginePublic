#pragma once

namespace engine { struct FrameStaticData; }

namespace game
{

struct AiSteeringResult
{
	XMVECTOR vecAiDirection;
};

AiSteeringResult XM_CALLCONV ComputeAiSteering(const engine::FrameStaticData& rStaticData, FXMVECTOR vecPosition, FXMVECTOR vecCurrentDirection, FXMVECTOR vecFrameCenter, float fDeltaTime, bool bAlternateContour);

float XM_CALLCONV ComputeTerrainAvoidance(const engine::FrameStaticData& rStaticData, FXMVECTOR vecPosition, FXMVECTOR vecDirection, float fCurrentDeltaRotation);

} // namespace game

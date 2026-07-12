#pragma once

namespace engine { struct FrameStaticData; }

namespace game
{

struct AiSteeringResult
{
	XMVECTOR vecAiDirection;
};

struct SegmentHit
{
	bool bHit = false;
	float fTime = 0.0f;
	XMVECTOR vecPosition {};
};

AiSteeringResult XM_CALLCONV ComputeAiSteering(const engine::FrameStaticData& rStaticData, FXMVECTOR vecPosition, FXMVECTOR vecCurrentDirection, FXMVECTOR vecFrameCenter, float fDeltaTime, bool bAlternateContour);

float XM_CALLCONV ComputeTerrainAvoidance(const engine::FrameStaticData& rStaticData, FXMVECTOR vecPosition, FXMVECTOR vecDirection, float fCurrentDeltaRotation);
SegmentHit XM_CALLCONV TracePointAgainstTerrain(const engine::FrameStaticData& rStaticData, FXMVECTOR vecStartPosition, FXMVECTOR vecEndPosition, float fStartTime, float fEndTime);
SegmentHit XM_CALLCONV TracePointToFrameExit(FXMVECTOR vecArea, FXMVECTOR vecStartPosition, FXMVECTOR vecEndPosition, float fStartTime, float fEndTime);

} // namespace game

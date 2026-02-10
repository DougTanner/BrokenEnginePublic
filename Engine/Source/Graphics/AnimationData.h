#pragma once

#include "DataFile.h"

namespace engine
{

class AnimationData
{
public:

	void Load(const byte* pAnimationData, common::crc_t crc);
	void Evaluate(int64_t iAnimationIndex, float fTime, int64_t iMaterialIndex, common::MeshData* pMeshData, XMFLOAT4X4* pJointMatrices, int64_t iJointMatrixOffset) const;
	int64_t FindAnimation(std::string_view name) const;

	common::crc_t mCrc = 0;
	common::AnimationHeader mHeader {};
	std::vector<common::AnimationChannel> mChannels;
	std::vector<common::AnimationKeyframe> mKeyframes;

private:

	XMVECTOR InterpolateKeyframes(const common::AnimationChannel& rChannel, float fTime) const;
	void EvaluateWorldMatrices(int64_t iAnimationIndex, float fTime, XMVECTOR* pTranslations, XMVECTOR* pRotations, XMVECTOR* pScales, XMMATRIX* pLocalMatrices, XMMATRIX* pWorldMatrices) const;
};

// Global registry by scene CRC
inline std::unordered_map<common::crc_t, AnimationData> gAnimationDataMap;

} // namespace engine

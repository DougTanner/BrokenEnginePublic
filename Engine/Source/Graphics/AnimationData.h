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

	const common::AnimationHeader& GetHeader() const { return mHeader; }
	common::crc_t GetCrc() const { return mCrc; }

private:

	XMVECTOR InterpolateKeyframes(const common::AnimationChannel& rChannel, float fTime) const;
	void EvaluateWorldMatrices(int64_t iAnimationIndex, float fTime, XMMATRIX* pWorldMatrices) const;

	common::crc_t mCrc = 0;
	common::AnimationHeader mHeader {};
	std::vector<common::AnimationChannel> mChannels;
	std::vector<common::AnimationKeyframe> mKeyframes;
};

// Global registry by scene CRC
inline std::unordered_map<common::crc_t, AnimationData> gAnimationDataMap;

} // namespace engine

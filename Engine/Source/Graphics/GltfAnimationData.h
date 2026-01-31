#pragma once

#include "DataFile.h"

namespace engine
{

class GltfAnimationData
{
public:

	void Load(const byte* pAnimationData, common::crc_t crc);
	void Evaluate(int64_t iAnimationIndex, float fTime, int64_t iMaterialIndex, common::MeshData* pMeshData, XMFLOAT4X4* pJointMatrices, int64_t iJointMatrixOffset) const;
	int64_t FindAnimation(std::string_view name) const;

	const common::GltfAnimationHeader& GetHeader() const { return mHeader; }
	common::crc_t GetCrc() const { return mCrc; }

private:

	XMVECTOR InterpolateKeyframes(const common::GltfAnimationChannel& rChannel, float fTime) const;
	void EvaluateWorldMatrices(int64_t iAnimationIndex, float fTime, XMMATRIX* pWorldMatrices) const;

	common::crc_t mCrc = 0;
	common::GltfAnimationHeader mHeader {};
	std::vector<common::GltfAnimationChannel> mChannels;
	std::vector<common::GltfAnimationKeyframe> mKeyframes;
};

// Global registry by GLTF CRC
inline std::unordered_map<common::crc_t, GltfAnimationData> gAnimationDataMap;

} // namespace engine

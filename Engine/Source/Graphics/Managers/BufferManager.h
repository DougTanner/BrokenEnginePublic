#pragma once

#include "Graphics/Objects/Buffer.h"

namespace engine
{

class DeviceManager;
struct RenderFrame;

class BufferManager
{
public:

	BufferManager();
	~BufferManager();

	void CreateTerrainMesh();
	void CreateWaterMesh();

	std::unordered_map<common::crc_t, Buffer> mModelMap;

	std::vector<Buffer> mGlobalLayoutUniformBuffers;
	std::vector<Buffer> mMainLayoutUniformBuffers;

	std::vector<Buffer> mVisibleLightsStorageBuffers;
	std::vector<Buffer> mAreaLightsStorageBuffers;
	std::vector<Buffer> mPointLightsStorageBuffers;

	std::vector<Buffer> mHexShieldsStorageBuffers;
	std::vector<Buffer> mBillboardsStorageBuffers;
	std::vector<Buffer> mTextStorageBuffers;
	std::vector<Buffer> mWidgetsStorageBuffers;
	// DT: GAMELOGIC
	std::vector<Buffer> mPlayerStorageBuffers;
	std::vector<Buffer> mPlayerMissilesStorageBuffers;
	std::vector<Buffer> mSpaceshipsStorageBuffers;

	std::vector<Buffer> mSmokeSpreadStorageBuffers;
	std::vector<Buffer> mSmokePuffsStorageBuffers;
	std::vector<Buffer> mSmokeTrailsStorageBuffers;

	Buffer mQuadsVertexBuffer;
	Buffer mTerrainMeshBuffer;
	Buffer mWaterMeshBuffer;

	std::vector<Buffer> mLongParticlesSpawnStorageBuffers;
	Buffer mLongParticlesStorageBuffer;

	std::vector<Buffer> mSquareParticlesSpawnStorageBuffers;
	Buffer mSquareParticlesStorageBuffer;

#if defined(ENABLE_GLTF_TEST)
	std::vector<Buffer> mGltfsStorageBuffers;
#endif
};

inline BufferManager* gpBufferManager = nullptr;

} // namespace engine

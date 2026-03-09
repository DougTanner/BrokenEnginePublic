#include "Islands.h"

#if defined(BT_CLIENT)

namespace engine
{

Islands::Islands()
{
	gpIslands = this;

	mIslands.resize(kiDefaultIslandCapacity);

	// Fill initial quads from base island template
	for (size_t i = 0; i < mIslands.size(); ++i)
	{
		if (i < static_cast<size_t>(game::Frame::kiIslandCount))
		{
			mIslands[i].quad.f4VertexRect.x = game::Frame::kpfIslandPositions[i][0];
			mIslands[i].quad.f4VertexRect.y = game::Frame::kpfIslandPositions[i][1];
			mIslands[i].quad.f4VertexRect.z = game::Frame::kpfIslandPositions[i][2];
			mIslands[i].quad.f4VertexRect.w = game::Frame::kpfIslandPositions[i][3];
		}
		else
		{
			mIslands[i].quad.f4VertexRect = {};
		}

		mIslands[i].quad.f4TextureRect.x = 0.0f;
		mIslands[i].quad.f4TextureRect.z = 1.0f;
		mIslands[i].quad.f4TextureRect.y = 0.0f;
		mIslands[i].quad.f4TextureRect.w = 1.0f;
	}

	// Calculate global area bounds from the base island template
	mf4GlobalArea.x = game::Frame::kfBaseAreaMinX;
	mf4GlobalArea.y = game::Frame::kfBaseAreaMaxY;
	mf4GlobalArea.z = game::Frame::kfBaseAreaMaxX;
	mf4GlobalArea.w = game::Frame::kfBaseAreaMinY;

	// Set beach elevation on all island slots
	for (Island& rIsland : mIslands)
	{
		rIsland.quad.f4Params.x = gpIslandTerrain->mfBeachElevation;
	}

	mIslandsStorageBuffer.Create(
	{
		.name = "Islands",
		.flags = {BufferFlags::kStorage, BufferFlags::kHostVisible},
		.iCount = static_cast<int64_t>(mIslands.size()),
		.iVertexStride = sizeof(shaders::AxisAlignedQuadLayout),
		.dataVkDeviceSize = mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout),
	});

	// Copy island quads to storage buffer for GPU rendering
	auto* pQuads = common::gpThreadLocal->mWorkbuffer.PushBuffer<shaders::AxisAlignedQuadLayout*>(mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
	for (size_t i = 0; i < mIslands.size(); ++i)
	{
		pQuads[i] = mIslands[i].quad;
	}
	memcpy(mIslandsStorageBuffer.mpMappedMemory, pQuads, mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
	common::gpThreadLocal->mWorkbuffer.Pop();
}

Islands::~Islands()
{
	gpIslands = nullptr;
}

void Islands::UpdateActiveIslands(const std::unordered_map<GridCoord, CoordFrames>& rFrames, const std::vector<GridCoord>& rActiveCoords)
{
	int64_t iActiveCount = static_cast<int64_t>(rActiveCoords.size());

	// Grow capacity if needed
	if (iActiveCount > static_cast<int64_t>(mIslands.size()))
	{
		int64_t iNewCapacity = static_cast<int64_t>(mIslands.size());
		while (iNewCapacity < iActiveCount)
		{
			iNewCapacity *= 2;
		}
		iNewCapacity = std::min(iNewCapacity, static_cast<int64_t>(shaders::kiMaxIslands));
		mIslands.resize(static_cast<size_t>(iNewCapacity));

		// Initialize new slots with beach elevation
		for (size_t i = static_cast<size_t>(iActiveCount); i < mIslands.size(); ++i)
		{
			mIslands[i].quad.f4Params.x = gpIslandTerrain->mfBeachElevation;
		}

		// Recreate storage buffer at new capacity
		mIslandsStorageBuffer.Destroy();
		mIslandsStorageBuffer.Create(
		{
			.name = "Islands",
			.flags = {BufferFlags::kStorage, BufferFlags::kHostVisible},
			.iCount = static_cast<int64_t>(mIslands.size()),
			.iVertexStride = sizeof(shaders::AxisAlignedQuadLayout),
			.dataVkDeviceSize = mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout),
		});

		// Re-record command buffers to pick up new instance count
		gpGraphics->meDestroyType = DestroyType::kCommandBuffers;
	}

	// Fill active island slots from frame data
	float fBaseWidth = game::Frame::kpfIslandPositions[0][2];
	float fBaseHeight = game::Frame::kpfIslandPositions[0][3];

	for (int64_t i = 0; i < iActiveCount; ++i)
	{
		const GridCoord& rCoord = rActiveCoords[static_cast<size_t>(i)];
		auto it = rFrames.find(rCoord);
		if (it == rFrames.end() || it->second.pCurrent == nullptr)
		{
			continue;
		}

		const game::Frame& rFrame = *it->second.pCurrent;
		IslandsFlip eFlip = rFrame.postRender.eIslandsFlip;

		Island& rIsland = mIslands[static_cast<size_t>(i)];
		bool bFlipX = eFlip == kFlipX || eFlip == kFlipXY;
		bool bFlipY = eFlip == kFlipY || eFlip == kFlipXY;

		// Position from base island template offset by grid coordinate
		rIsland.quad.f4VertexRect.x = game::Frame::kfBaseAreaMinX + static_cast<float>(rCoord.x) * fBaseWidth;
		rIsland.quad.f4VertexRect.y = game::Frame::kfBaseAreaMaxY + static_cast<float>(rCoord.y) * std::abs(fBaseHeight);
		rIsland.quad.f4VertexRect.z = fBaseWidth;
		rIsland.quad.f4VertexRect.w = fBaseHeight;

		// Texture coords from flip state
		rIsland.quad.f4TextureRect.x = bFlipX ? 1.0f : 0.0f;
		rIsland.quad.f4TextureRect.z = bFlipX ? 0.0f : 1.0f;
		rIsland.quad.f4TextureRect.y = bFlipY ? 1.0f : 0.0f;
		rIsland.quad.f4TextureRect.w = bFlipY ? 0.0f : 1.0f;

		// Params: beach elevation, flipX, flipY
		rIsland.quad.f4Params.x = gpIslandTerrain->mfBeachElevation;
		rIsland.quad.f4Params.y = bFlipX ? 1.0f : 0.0f;
		rIsland.quad.f4Params.z = bFlipY ? 1.0f : 0.0f;
	}

	// Zero remaining slots (zero-width quads -> GPU culled)
	for (size_t i = static_cast<size_t>(iActiveCount); i < mIslands.size(); ++i)
	{
		mIslands[i].quad.f4VertexRect = {};
	}

	// Recompute global area from active islands
	mf4GlobalArea.x = std::numeric_limits<float>::max();
	mf4GlobalArea.y = std::numeric_limits<float>::lowest();
	mf4GlobalArea.z = std::numeric_limits<float>::lowest();
	mf4GlobalArea.w = std::numeric_limits<float>::max();
	for (int64_t i = 0; i < iActiveCount; ++i)
	{
		const Island& rIsland = mIslands[static_cast<size_t>(i)];
		mf4GlobalArea.x = std::min(mf4GlobalArea.x, rIsland.quad.f4VertexRect.x);
		mf4GlobalArea.y = std::max(mf4GlobalArea.y, rIsland.quad.f4VertexRect.y);
		mf4GlobalArea.z = std::max(mf4GlobalArea.z, rIsland.quad.f4VertexRect.x + rIsland.quad.f4VertexRect.z);
		mf4GlobalArea.w = std::min(mf4GlobalArea.w, rIsland.quad.f4VertexRect.y + rIsland.quad.f4VertexRect.w);
	}

	// Upload all quads to storage buffer
	auto* pQuads = common::gpThreadLocal->mWorkbuffer.PushBuffer<shaders::AxisAlignedQuadLayout*>(mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
	for (size_t i = 0; i < mIslands.size(); ++i)
	{
		pQuads[i] = mIslands[i].quad;
	}
	memcpy(mIslandsStorageBuffer.mpMappedMemory, pQuads, mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
	common::gpThreadLocal->mWorkbuffer.Pop();
}

} // namespace engine

#endif // BT_CLIENT

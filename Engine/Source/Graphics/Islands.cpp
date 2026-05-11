#include "Islands.h"

#if defined(BT_CLIENT)

#include "Frame/FrameStaticData.h"
#include "Frame/IslandPlacement.h"

namespace engine
{

Islands::Islands()
{
	gpIslands = this;

	mIslands.resize(kiDefaultIslandCapacity);

	// Fill initial quads from the origin cell's placement list. Texture-slot bootstrap is
	// deferred to TextureManager (constructed after Islands); origin uses kIslands01Crc which
	// TextureManager deterministically assigns to slot 0, so slot 0 is correct here.
	std::vector<IslandPlacement> originPlacements;
	GenerateIslandPlacements(kOriginCoord, originPlacements);
	const IslandPlacement& rOrigin = originPlacements.at(0);
	const IslandTemplate& rOriginTemplate = gpIslandTerrain->mIslands.at(rOrigin.islandCrc);
	for (size_t i = 0; i < mIslands.size(); ++i)
	{
		if (i == 0)
		{
			mIslands[i].quad.f4VertexRect.x = rOrigin.f2WorldPos.x - 0.5f * rOriginTemplate.mfQuadWidth;
			mIslands[i].quad.f4VertexRect.y = rOrigin.f2WorldPos.y + 0.5f * rOriginTemplate.mfQuadHeight;
			mIslands[i].quad.f4VertexRect.z = rOriginTemplate.mfQuadWidth;
			mIslands[i].quad.f4VertexRect.w = -rOriginTemplate.mfQuadHeight;
		}
		else
		{
			mIslands[i].quad.f4VertexRect = {};
		}

		mIslands[i].quad.uiTextureSlot = 0;

		mIslands[i].quad.f4TextureRect.x = 0.0f;
		mIslands[i].quad.f4TextureRect.z = 1.0f;
		mIslands[i].quad.f4TextureRect.y = 0.0f;
		mIslands[i].quad.f4TextureRect.w = 1.0f;
	}

	// Calculate global area bounds from the base cell
	mf4GlobalArea.x = game::Frame::kfBaseAreaMinX;
	mf4GlobalArea.y = game::Frame::kfBaseAreaMaxY;
	mf4GlobalArea.z = game::Frame::kfBaseAreaMaxX;
	mf4GlobalArea.w = game::Frame::kfBaseAreaMinY;

	// Beach elevation default on all slots; UpdateActiveIslands fills active slots. fRotation defaults to 0 (identity).
	for (Island& rIsland : mIslands)
	{
		rIsland.quad.f4Params.x = gpIslandTerrain->mpCanonical->mfBeachElevation;
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
	auto pQuads = common::gpThreadLocal->mWorkbuffer.PushBuffer<shaders::AxisAlignedQuadLayout*>(mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
	for (size_t i = 0; i < mIslands.size(); ++i)
	{
		pQuads[i] = mIslands[i].quad;
	}
	memcpy(mIslandsStorageBuffer.mpMappedMemory, pQuads, mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
}

Islands::~Islands()
{
	gpIslands = nullptr;
}

void Islands::UpdateActiveIslands(const std::unordered_map<GridCoord, CoordFrames>& rFrames, const std::vector<GridCoord>& rActiveCoords)
{
	// Total instance count is sum of placements across all active coords (Phase 4 multi-emit).
	int64_t iTotalPlacements = 0;
	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto itCount = rFrames.find(rCoord);
		if (itCount != rFrames.end() && itCount->second.iSnapshotCount > 0)
		{
			iTotalPlacements += static_cast<int64_t>(itCount->second.staticData.islands.size());
		}
	}
	iTotalPlacements = std::min(iTotalPlacements, static_cast<int64_t>(shaders::kiMaxIslands));

	// Grow capacity if needed
	if (iTotalPlacements > static_cast<int64_t>(mIslands.size()))
	{
		int64_t iNewCapacity = static_cast<int64_t>(mIslands.size());
		while (iNewCapacity < iTotalPlacements)
		{
			iNewCapacity *= 2;
		}
		iNewCapacity = std::min(iNewCapacity, static_cast<int64_t>(shaders::kiMaxIslands));
		mIslands.resize(static_cast<size_t>(iNewCapacity));

		// Initialize new slots with beach elevation; fRotation defaults to 0 (identity).
		for (size_t i = static_cast<size_t>(iTotalPlacements); i < mIslands.size(); ++i)
		{
			mIslands[i].quad.f4Params.x = gpIslandTerrain->mpCanonical->mfBeachElevation;
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

	// Fan out per-cell placements into the flat storage buffer.
	int64_t iEmitIndex = 0;
	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = rFrames.find(rCoord);
		if (it == rFrames.end() || it->second.iSnapshotCount == 0)
		{
			continue;
		}

		const FrameStaticData& rStaticData = it->second.staticData;
		for (const IslandPlacement& rPlacement : rStaticData.islands)
		{
			if (iEmitIndex >= static_cast<int64_t>(shaders::kiMaxIslands))
			{
				break;
			}

			const IslandTemplate& rTemplate = gpIslandTerrain->mIslands.at(rPlacement.islandCrc);
			Island& rIsland = mIslands[static_cast<size_t>(iEmitIndex)];

			rIsland.quad.f4VertexRect.x = rPlacement.f2WorldPos.x - 0.5f * rTemplate.mfQuadWidth;
			rIsland.quad.f4VertexRect.y = rPlacement.f2WorldPos.y + 0.5f * rTemplate.mfQuadHeight;
			rIsland.quad.f4VertexRect.z = rTemplate.mfQuadWidth;
			rIsland.quad.f4VertexRect.w = -rTemplate.mfQuadHeight;

			rIsland.quad.f4TextureRect.x = 0.0f;
			rIsland.quad.f4TextureRect.z = 1.0f;
			rIsland.quad.f4TextureRect.y = 0.0f;
			rIsland.quad.f4TextureRect.w = 1.0f;

			rIsland.quad.f4Params.x = rTemplate.mfBeachElevation;
			rIsland.quad.fRotation = rPlacement.fRotation;
			rIsland.quad.uiTextureSlot = static_cast<uint32_t>(gpIslandTerrain->AcquireTextureSlot(rPlacement.islandCrc));
			++iEmitIndex;
		}
		if (iEmitIndex >= static_cast<int64_t>(shaders::kiMaxIslands))
		{
			break;
		}
	}

	// Zero remaining slots (zero-width quads -> GPU culled)
	for (size_t i = static_cast<size_t>(iEmitIndex); i < mIslands.size(); ++i)
	{
		mIslands[i].quad.f4VertexRect = {};
	}

	// Recompute global area from emitted islands. Each island's rotated rectangle expands its
	// axis-aligned bound to half-extents (|w|*|cos|+|h|*|sin|, |w|*|sin|+|h|*|cos|) about its center.
	mf4GlobalArea.x = std::numeric_limits<float>::max();
	mf4GlobalArea.y = std::numeric_limits<float>::lowest();
	mf4GlobalArea.z = std::numeric_limits<float>::lowest();
	mf4GlobalArea.w = std::numeric_limits<float>::max();
	for (int64_t i = 0; i < iEmitIndex; ++i)
	{
		const Island& rIsland = mIslands[static_cast<size_t>(i)];
		float fHalfW = 0.5f * std::abs(rIsland.quad.f4VertexRect.z);
		float fHalfH = 0.5f * std::abs(rIsland.quad.f4VertexRect.w);
		float fAbsCos = std::abs(std::cos(rIsland.quad.fRotation));
		float fAbsSin = std::abs(std::sin(rIsland.quad.fRotation));
		float fRotHalfW = fHalfW * fAbsCos + fHalfH * fAbsSin;
		float fRotHalfH = fHalfW * fAbsSin + fHalfH * fAbsCos;
		float fCenterX = rIsland.quad.f4VertexRect.x + 0.5f * rIsland.quad.f4VertexRect.z;
		float fCenterY = rIsland.quad.f4VertexRect.y + 0.5f * rIsland.quad.f4VertexRect.w; // w negative
		mf4GlobalArea.x = std::min(mf4GlobalArea.x, fCenterX - fRotHalfW);
		mf4GlobalArea.y = std::max(mf4GlobalArea.y, fCenterY + fRotHalfH);
		mf4GlobalArea.z = std::max(mf4GlobalArea.z, fCenterX + fRotHalfW);
		mf4GlobalArea.w = std::min(mf4GlobalArea.w, fCenterY - fRotHalfH);
	}

	// Upload all quads to storage buffer
	auto pQuads = common::gpThreadLocal->mWorkbuffer.PushBuffer<shaders::AxisAlignedQuadLayout*>(mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
	for (size_t i = 0; i < mIslands.size(); ++i)
	{
		pQuads[i] = mIslands[i].quad;
	}
	memcpy(mIslandsStorageBuffer.mpMappedMemory, pQuads, mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
}

} // namespace engine

#endif // BT_CLIENT

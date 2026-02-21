#include "FrameGrid.h"

#include "Frame/Frame.h"

namespace engine
{

namespace
{

// Copy one SOA member's data from source to destination at a given offset.
// Handles both single pointer members (T*) and array-of-pointer members (T*[N]).
template<typename T>
void CopyMemberRange(T& rDest, int64_t iDestOffset, const T& rSrc, int64_t iSrcCount)
{
	using RawT = std::remove_reference_t<T>;
	if constexpr (std::is_array_v<RawT>)
	{
		constexpr size_t N = std::extent_v<RawT>;
		using ElemPtr = std::remove_extent_t<RawT>;
		using Elem = std::remove_pointer_t<ElemPtr>;
		for (size_t i = 0; i < N; ++i)
		{
			std::memcpy(rDest[i] + iDestOffset, rSrc[i], iSrcCount * sizeof(Elem));
		}
	}
	else
	{
		using Elem = std::remove_pointer_t<RawT>;
		std::memcpy(rDest + iDestOffset, rSrc, iSrcCount * sizeof(Elem));
	}
}

// Copy all SOA members from source tuple to destination tuple at a given offset
template<size_t... Is, typename TDest, typename TSrc>
void CopyAllMembersImpl(TDest&& rDestTuple, TSrc&& rSrcTuple, int64_t iDestOffset, int64_t iSrcCount, std::index_sequence<Is...>)
{
	(CopyMemberRange(std::get<Is>(rDestTuple), iDestOffset, std::get<Is>(rSrcTuple), iSrcCount), ...);
}

template<typename TDest, typename TSrc>
void CopyAllMembers(TDest&& rDestTuple, TSrc&& rSrcTuple, int64_t iDestOffset, int64_t iSrcCount)
{
	constexpr size_t N = std::tuple_size_v<std::remove_cvref_t<TDest>>;
	CopyAllMembersImpl(std::forward<TDest>(rDestTuple), std::forward<TSrc>(rSrcTuple), iDestOffset, iSrcCount, std::make_index_sequence<N>{});
}

// Add world-space offset to a range of position vectors
void XM_CALLCONV OffsetPositions(XMVECTOR* __restrict pPositions, int64_t iOffset, int64_t iCount, FXMVECTOR vecOffset)
{
	for (int64_t i = 0; i < iCount; ++i)
	{
		pPositions[iOffset + i] = XMVectorAdd(pPositions[iOffset + i], vecOffset);
	}
}

struct InterpolatedFrame
{
	GridCoord coord;
	game::FrameInterpolate interpolate {};
	XMVECTOR vecOffset {};
};

} // anonymous namespace

void MergeFramesForRender(game::FrameInterpolate& rDest, const std::unordered_map<GridCoord, std::unique_ptr<game::Frame>>& rCurrentFrames, const std::vector<GridCoord>& rActiveCoords, GridCoord cameraCoord, float fDeltaTime)
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	// World coordinates: positions are absolute, no per-frame offset needed.
	const game::Frame& rCameraFrame = *rCurrentFrames.at(cameraCoord);

	// Interpolate each active frame.
	// Camera frame is processed first so human player retains index 0 in merged collections.
	// Uses unique_ptr because FrameInterpolate is not copyable (contains unique_ptr SOA buffers).
	std::vector<std::unique_ptr<InterpolatedFrame>> frames;
	frames.reserve(rActiveCoords.size());

	{
		auto& pFrame = frames.emplace_back(std::make_unique<InterpolatedFrame>());
		pFrame->coord = cameraCoord;
		pFrame->vecOffset = XMVectorZero();
		game::FrameInterpolate::AllocateAndCopy(pFrame->interpolate, rCameraFrame.interpolate);
		game::FrameInterpolate::Update(pFrame->interpolate, rCameraFrame, fDeltaTime);
	}

	for (const GridCoord& rCoord : rActiveCoords)
	{
		if (rCoord == cameraCoord)
		{
			continue;
		}
		auto it = rCurrentFrames.find(rCoord);
		if (it == rCurrentFrames.end() || it->second == nullptr)
		{
			continue;
		}

		auto& pFrame = frames.emplace_back(std::make_unique<InterpolatedFrame>());
		pFrame->coord = rCoord;
		pFrame->vecOffset = XMVectorZero();
		game::FrameInterpolate::AllocateAndCopy(pFrame->interpolate, it->second->interpolate);
		game::FrameInterpolate::Update(pFrame->interpolate, *it->second, fDeltaTime);
	}

	int64_t iFrameCount = static_cast<int64_t>(frames.size());

	// Copy frame-level scalar fields from camera frame
	static_cast<FrameInterpolateBase&>(rDest).frameFlags = frames.at(0)->interpolate.frameFlags;
	rDest.iFrame = frames.at(0)->interpolate.iFrame;
	rDest.fCurrentTime = frames.at(0)->interpolate.fCurrentTime;
	rDest.fDeltaTime = frames.at(0)->interpolate.fDeltaTime;
	rDest.gameFlags = frames.at(0)->interpolate.gameFlags;
	rDest.fSpawnTimer = frames.at(0)->interpolate.fSpawnTimer;

	// Merge one collection: count totals, allocate, copy data, apply position offsets
	auto mergeCollection = [&]<typename TCollection>(TCollection& rDestCol, auto getCollection, auto offsetFn)
	{
		int64_t iTotal = 0;
		for (int64_t i = 0; i < iFrameCount; ++i)
		{
			iTotal += getCollection(frames[i]->interpolate).iCount;
		}

		rDestCol.iCount = iTotal;
		if (iTotal == 0)
		{
			ResetDataToNull(rDestCol, rDestCol.Members());
			return;
		}

		AllocateAndAssign(rDestCol, iTotal, rDestCol.Members());

		int64_t iOff = 0;
		for (int64_t i = 0; i < iFrameCount; ++i)
		{
			const TCollection& rSrc = getCollection(frames[i]->interpolate);
			if (rSrc.iCount == 0)
			{
				continue;
			}

			CopyAllMembers(rDestCol.Members(), rSrc.Members(), iOff, rSrc.iCount);
			offsetFn(rDestCol, iOff, rSrc.iCount, frames[i]->vecOffset);

			iOff += rSrc.iCount;
		}
	};

	// Engine base collections
	mergeCollection(rDest.areaLights,
		[](const game::FrameInterpolate& rF) -> const AreaLightsInterpolate& { return rF.areaLights; },
		[](AreaLightsInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			for (int64_t k = 0; k < 4; ++k)
			{
				OffsetPositions(rD.pVecVisiblePositions[k], iOff, iCount, vecOff);
			}
		});

	mergeCollection(rDest.billboards,
		[](const game::FrameInterpolate& rF) -> const BillboardsInterpolate& { return rF.billboards; },
		[](BillboardsInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			OffsetPositions(rD.pVecPositions, iOff, iCount, vecOff);
		});

	mergeCollection(rDest.explosions,
		[](const game::FrameInterpolate& rF) -> const ExplosionsInterpolate& { return rF.explosions; },
		[](ExplosionsInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			OffsetPositions(rD.pVecPositions, iOff, iCount, vecOff);
			for (int64_t k = 0; k < kiMaxExplosionTrails; ++k)
			{
				OffsetPositions(rD.pVecTrailStartPositions[k], iOff, iCount, vecOff);
				OffsetPositions(rD.pVecTrailEndPositions[k], iOff, iCount, vecOff);
			}
		});

	mergeCollection(rDest.hexShields,
		[](const game::FrameInterpolate& rF) -> const HexShieldsInterpolate& { return rF.hexShields; },
		[](HexShieldsInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			OffsetPositions(rD.pVecPositions, iOff, iCount, vecOff);
		});

	mergeCollection(rDest.pointLights,
		[](const game::FrameInterpolate& rF) -> const PointLightsInterpolate& { return rF.pointLights; },
		[](PointLightsInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			OffsetPositions(rD.pVecPositions, iOff, iCount, vecOff);
		});

	mergeCollection(rDest.puffs,
		[](const game::FrameInterpolate& rF) -> const PuffsInterpolate& { return rF.puffs; },
		[](PuffsInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			OffsetPositions(rD.pVecPositions, iOff, iCount, vecOff);
		});

	mergeCollection(rDest.pushers,
		[](const game::FrameInterpolate& rF) -> const PushersInterpolate& { return rF.pushers; },
		[](PushersInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			OffsetPositions(rD.pVecPositions, iOff, iCount, vecOff);
		});

	mergeCollection(rDest.sounds,
		[](const game::FrameInterpolate& rF) -> const SoundsInterpolate& { return rF.sounds; },
		[](SoundsInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			OffsetPositions(rD.pVecPositions, iOff, iCount, vecOff);
		});

	mergeCollection(rDest.smokeTrails,
		[](const game::FrameInterpolate& rF) -> const SmokeTrailsInterpolate& { return rF.smokeTrails; },
		[](SmokeTrailsInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			OffsetPositions(rD.pVecPositions, iOff, iCount, vecOff);
		});

	mergeCollection(rDest.windRadials,
		[](const game::FrameInterpolate& rF) -> const WindRadialsInterpolate& { return rF.windRadials; },
		[](WindRadialsInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			OffsetPositions(rD.pVecPositions, iOff, iCount, vecOff);
		});

	mergeCollection(rDest.windTrails,
		[](const game::FrameInterpolate& rF) -> const WindTrailsInterpolate& { return rF.windTrails; },
		[](WindTrailsInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			OffsetPositions(rD.pVecPositions, iOff, iCount, vecOff);
		});

	// Game collections
	mergeCollection(rDest.players,
		[](const game::FrameInterpolate& rF) -> const game::PlayersInterpolate& { return rF.players; },
		[](game::PlayersInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			OffsetPositions(rD.pVecPositions, iOff, iCount, vecOff);
		});

	mergeCollection(rDest.blasters,
		[](const game::FrameInterpolate& rF) -> const game::BlastersInterpolate& { return rF.blasters; },
		[](game::BlastersInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			OffsetPositions(rD.pVecPositions, iOff, iCount, vecOff);
		});

	mergeCollection(rDest.missiles,
		[](const game::FrameInterpolate& rF) -> const game::MissilesInterpolate& { return rF.missiles; },
		[](game::MissilesInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			OffsetPositions(rD.pVecPositions, iOff, iCount, vecOff);
		});

	mergeCollection(rDest.spaceships,
		[](const game::FrameInterpolate& rF) -> const game::SpaceshipsInterpolate& { return rF.spaceships; },
		[](game::SpaceshipsInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			OffsetPositions(rD.pVecPositions, iOff, iCount, vecOff);
		});

	mergeCollection(rDest.targets,
		[](const game::FrameInterpolate& rF) -> const game::TargetsInterpolate& { return rF.targets; },
		[](game::TargetsInterpolate& rD, int64_t iOff, int64_t iCount, XMVECTOR vecOff)
		{
			OffsetPositions(rD.pVecPositions, iOff, iCount, vecOff);
		});

	// Rebuild players idToIndexMap (needed by Camera for human player lookup)
	rDest.players.idToIndexMap.clear();
	int64_t iPlayerOffset = 0;
	for (int64_t i = 0; i < iFrameCount; ++i)
	{
		const game::PlayersInterpolate& rSrc = frames[i]->interpolate.players;
		for (const auto& [rId, iIndex] : rSrc.idToIndexMap)
		{
			rDest.players.idToIndexMap[rId] = iIndex + iPlayerOffset;
		}
		iPlayerOffset += rSrc.iCount;
	}
}

} // namespace engine

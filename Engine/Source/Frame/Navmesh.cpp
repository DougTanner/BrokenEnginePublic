#include "Navmesh.h"

#include "Graphics/Islands.h"

#include "Frame/Frame.h"

using namespace DirectX;

namespace engine
{

inline int64_t XToI(DirectX::XMFLOAT4 f4GlobalVisibleArea, float fX)
{
	float fLeft = f4GlobalVisibleArea.x;
	float fRight = f4GlobalVisibleArea.z;

	int64_t i = static_cast<int64_t>(static_cast<float>(Navmesh::kiGrid - 1) * ((fX - fLeft) / (fRight - fLeft)));
	if (!(i >= 0 && i < Navmesh::kiGrid)) [[unlikely]]
	{
		DEBUG_BREAK();
		return 0;
	}
	return i;
}

inline float IToX(DirectX::XMFLOAT4 f4GlobalVisibleArea, int64_t i)
{
	if (!(i >= 0 && i < Navmesh::kiGrid)) [[unlikely]]
	{
		DEBUG_BREAK();
		return 0.0f;
	}

	float fLeft = f4GlobalVisibleArea.x;
	float fRight = f4GlobalVisibleArea.z;
	float fDeltaX = (fRight - fLeft) / static_cast<float>(Navmesh::kiGrid - 1);

	return fLeft + static_cast<float>(i) * fDeltaX;
}

inline int64_t YToJ(DirectX::XMFLOAT4 f4GlobalVisibleArea, float fY)
{
	float fTop = f4GlobalVisibleArea.y;
	float fBottom = f4GlobalVisibleArea.w;

	int64_t j = static_cast<int64_t>(static_cast<float>(Navmesh::kiGrid - 1) * (1.0f - (fY - fBottom) / (fTop - fBottom)));
	if (!(j >= 0 && j < Navmesh::kiGrid)) [[unlikely]]
	{
		DEBUG_BREAK();
		return 0;
	}
	return j;
}

inline float JToY(DirectX::XMFLOAT4 f4GlobalVisibleArea, int64_t j)
{
	if (!(j >= 0 && j < Navmesh::kiGrid)) [[unlikely]]
	{
		DEBUG_BREAK();
		return 0.0f;
	}

	float fTop = f4GlobalVisibleArea.y;
	float fBottom = f4GlobalVisibleArea.w;
	float fDeltaY = (fBottom - fTop) / static_cast<float>(Navmesh::kiGrid - 1);

	return fTop + static_cast<float>(j) * fDeltaY;
}

void Navmesh::SetupGrid(DirectX::XMFLOAT4 f4GlobalVisibleArea, Navmesh& __restrict rNavmesh)
{
	Navmesh& rCurrent = rNavmesh;

	for (int64_t j = 0; j < kiGrid; ++j)
	{
		float fY = JToY(f4GlobalVisibleArea, j);

		for (int64_t i = 0; i < kiGrid; ++i)
		{
			float fX = IToX(f4GlobalVisibleArea, i);
			float fZ = engine::gpIslands->GlobalElevation(XMVectorSet(fX, fY, 0.0f, 1.0f));
			rCurrent.ppbGridNavigable[j][i] = fZ < kfHeight;
		}
	}
}

void Navmesh::SetupPlayerDistances(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame)
{
	SCOPED_CPU_PROFILE(kCpuTimerPlayerDistances);

	Navmesh& rCurrent = rFrame.navmesh;
	const Navmesh& rPrevious = rPreviousFrame.navmesh;

	rCurrent.iLastPlayerX = rPrevious.iLastPlayerX;
	rCurrent.iLastPlayerY = rPrevious.iLastPlayerY;
	memcpy(&rCurrent.ppbGridNavigable[0], &rPrevious.ppbGridNavigable[0], sizeof(ppbGridNavigable));
#if defined(ENABLE_NAVMESH_DISPLAY)
	memcpy(&rCurrent.ppuiBillboards[0], &rPrevious.ppuiBillboards[0], sizeof(ppuiBillboards));
	rCurrent.uiBillboard = rPrevious.uiBillboard;
#endif

	int64_t iPlayerX = XToI(rFrame.f4GlobalArea, std::clamp(XMVectorGetX(rFrame.player.vecPosition), rFrame.f4GlobalArea.x, rFrame.f4GlobalArea.z));
	int64_t iPlayerY = YToJ(rFrame.f4GlobalArea, std::clamp(XMVectorGetY(rFrame.player.vecPosition), rFrame.f4GlobalArea.w, rFrame.f4GlobalArea.y));

	if (iPlayerX == rCurrent.iLastPlayerX && iPlayerY == rCurrent.iLastPlayerY)
	{
		return;
	}
	rCurrent.iLastPlayerX = iPlayerX;
	rCurrent.iLastPlayerY = iPlayerY;

	int64_t iNavigableX = iPlayerX;
	int64_t iNavigableY = iPlayerY;
	int64_t iSearchDistance = 1;
	while (iSearchDistance < Navmesh::kiGrid / 2 && !rCurrent.ppbGridNavigable[iNavigableY][iNavigableX])
	{
		for (int64_t j = 0; j < iSearchDistance; ++j)
		{
			int64_t iSearchY = iPlayerY - iSearchDistance / 2 + j;
			if (iSearchY < 0 || iSearchY >= Navmesh::kiGrid)
			{
				continue;
			}

			for (int64_t i = 0; i < iSearchDistance; ++i)
			{
				int64_t iSearchX = iPlayerX - iSearchDistance / 2 + i;
				if (iSearchX < 0 || iSearchX >= Navmesh::kiGrid)
				{
					continue;
				}

				if (rCurrent.ppbGridNavigable[iSearchY][iSearchX])
				{
					iNavigableX = iSearchX;
					iNavigableY = iSearchY;
				}
			}
		}

		iSearchDistance += 2;
	}

	// LOG("\nPlayer navigable: {}, {} ({})", iNavigableX, iNavigableY, iSearchDistance);

	for (int64_t j = 0; j < kiGrid; ++j)
	{
		for (int64_t i = 0; i < kiGrid; ++i)
		{
			smppuiPlayerDistances[j][i] = std::numeric_limits<uint8_t>::max();
		}
	}
	smppuiPlayerDistances[iNavigableY][iNavigableX] = 1;

	bool bChanged = false;
	do
	{
		bChanged = false;

		for (int64_t j = 0; j < kiGrid; ++j)
		{
			for (int64_t i = 0; i < kiGrid; ++i)
			{
				if (!rCurrent.ppbGridNavigable[j][i])
				{
					continue;
				}
					
				if (i < kiGrid - 1 && (smppuiPlayerDistances[j][i + 1] < smppuiPlayerDistances[j][i] - 1))
				{
					smppuiPlayerDistances[j][i] = smppuiPlayerDistances[j][i + 1] + 1;
					bChanged = true;
				}

				if (i > 0 && (smppuiPlayerDistances[j][i - 1] < smppuiPlayerDistances[j][i] - 1))
				{
					smppuiPlayerDistances[j][i] = smppuiPlayerDistances[j][i - 1] + 1;
					bChanged = true;
				}

				if (j < kiGrid - 1 && (smppuiPlayerDistances[j + 1][i] < smppuiPlayerDistances[j][i] - 1))
				{
					smppuiPlayerDistances[j][i] = smppuiPlayerDistances[j + 1][i] + 1;
					bChanged = true;
				}

				if (j > 0 && (smppuiPlayerDistances[j - 1][i] < smppuiPlayerDistances[j][i] - 1))
				{
					smppuiPlayerDistances[j][i] = smppuiPlayerDistances[j - 1][i] + 1;
					bChanged = true;
				}
			}
		}
	}
	while (bChanged);

#if defined(ENABLE_NAVMESH_DISPLAY)
	using namespace data;
	common::crc_t pCrcs[55] =
	{
		kTexturesParticlesBC4Square0pngCrc, kTexturesParticlesBC4Square1pngCrc, kTexturesParticlesBC4Square2pngCrc, kTexturesParticlesBC4Square3pngCrc, kTexturesParticlesBC4Square4pngCrc, kTexturesParticlesBC4Square5pngCrc, kTexturesParticlesBC4Square6pngCrc, kTexturesParticlesBC4Square7pngCrc, kTexturesParticlesBC4Square8pngCrc, kTexturesParticlesBC4Square9pngCrc, kTexturesParticlesBC4Square10pngCrc, kTexturesParticlesBC4Square11pngCrc, kTexturesParticlesBC4Square12pngCrc, kTexturesParticlesBC4Square13pngCrc, kTexturesParticlesBC4Square14pngCrc, kTexturesParticlesBC4Square15pngCrc, kTexturesParticlesBC4Square16pngCrc, kTexturesParticlesBC4Square17pngCrc, kTexturesParticlesBC4Square18pngCrc, kTexturesParticlesBC4Square19pngCrc, kTexturesParticlesBC4Square20pngCrc, kTexturesParticlesBC4Square21pngCrc, kTexturesParticlesBC4Square22pngCrc, kTexturesParticlesBC4Square23pngCrc, kTexturesParticlesBC4Square24pngCrc, kTexturesParticlesBC4Square25pngCrc, kTexturesParticlesBC4Square26pngCrc, kTexturesParticlesBC4Square27pngCrc, kTexturesParticlesBC4Square28pngCrc, kTexturesParticlesBC4Square29pngCrc, kTexturesParticlesBC4Square30pngCrc, kTexturesParticlesBC4Square31pngCrc, kTexturesParticlesBC4Square32pngCrc, kTexturesParticlesBC4Square33pngCrc, kTexturesParticlesBC4Square34pngCrc, kTexturesParticlesBC4Square35pngCrc, kTexturesParticlesBC4Square36pngCrc, kTexturesParticlesBC4Square37pngCrc, kTexturesParticlesBC4Square38pngCrc, kTexturesParticlesBC4Square39pngCrc, kTexturesParticlesBC4Square40pngCrc, kTexturesParticlesBC4Square41pngCrc, kTexturesParticlesBC4Square42pngCrc, kTexturesParticlesBC4Square43pngCrc, kTexturesParticlesBC4Square44pngCrc, kTexturesParticlesBC4Square45pngCrc, kTexturesParticlesBC4Square46pngCrc, kTexturesParticlesBC4Square47pngCrc, kTexturesParticlesBC4Square48pngCrc, kTexturesParticlesBC4Square49pngCrc, kTexturesParticlesBC4Square50pngCrc, kTexturesParticlesBC4Square51pngCrc, kTexturesParticlesBC4Square52pngCrc, kTexturesParticlesBC4Square53pngCrc, kTexturesParticlesBC4Square54pngCrc,
	};
	for (int64_t j = 0; j < kiGrid; ++j)
	{
		float fY = JToY(j);

		for (int64_t i = 0; i < kiGrid; ++i)
		{
			if (!rCurrent.ppbGridNavigable[j][i])
			{
				rFrame.billboards.Remove(rCurrent.ppuiBillboards[j][i]);
				continue;
			}

			float fX = IToX(i);

			rFrame.billboards.Add(rCurrent.ppuiBillboards[j][i],
			{
				.flags = {engine::BillboardFlags::kTypeNone},
				.crc = pCrcs[std::min(Navmesh::smppuiPlayerDistances[j][i], 54ui8)],
				.fSize = 0.1f,
				.fAlpha = 1.0f,
				.fRotation = 0.0f,
				.vecPosition = XMVectorSet(fX, fY, gBaseHeight.Get(), 1.0f),
			});
		}
	}
#endif
}

XMVECTOR XM_CALLCONV Navmesh::NodeToPlayer([[maybe_unused]] game::Frame& __restrict rFrame, DirectX::XMVECTOR vecPosition)
{
	// If outside grid area, head towards zero
	float fX = XMVectorGetX(vecPosition);
	float fY = XMVectorGetY(vecPosition);
	if (fX <= rFrame.f4GlobalArea.x || fX >= rFrame.f4GlobalArea.z || fY >= rFrame.f4GlobalArea.y || fY <= rFrame.f4GlobalArea.w)
	{
		// LOG("  Outside head to zero");
		return XMVectorSet(0.0f, 0.0f, gBaseHeight.Get(), 1.0f);
	}

	int64_t iPositionX = XToI(rFrame.f4GlobalArea, fX);
	int64_t iPositionY = YToJ(rFrame.f4GlobalArea, fY);

	int64_t iDistance = -1;
	int64_t iNodeX = iPositionX;
	int64_t iNodeY = iPositionY;
	int64_t iSearchDistance = 1;
	while (iSearchDistance < Navmesh::kiGrid / 2 && iDistance == -1)
	{
		for (int64_t j = 0; j < iSearchDistance; ++j)
		{
			int64_t iSearchY = iPositionY - iSearchDistance / 2 + j;
			if (iSearchY < 0 || iSearchY >= Navmesh::kiGrid)
			{
				continue;
			}

			for (int64_t i = 0; i < iSearchDistance; ++i)
			{
				int64_t iSearchX = iPositionX - iSearchDistance / 2 + i;
				if (iSearchX < 0 || iSearchX >= Navmesh::kiGrid)
				{
					continue;
				}

				if (iSearchX == iPositionX && iSearchY == iPositionY)
				{
					continue;
				}

				if (Navmesh::smppuiPlayerDistances[iSearchY][iSearchX] == std::numeric_limits<uint8_t>::max())
				{
					continue;
				}

				if (iDistance == -1 || Navmesh::smppuiPlayerDistances[iSearchY][iSearchX] < iDistance)
				{
					iDistance = Navmesh::smppuiPlayerDistances[iSearchY][iSearchX];
					iNodeX = iSearchX;
					iNodeY = iSearchY;
				}
			}
		}

		iSearchDistance += 2;
	}

	// LOG("  From {}, {} to {}, {} ({}) distance {}", iPositionX, iPositionY, iNodeX, iNodeY, iSearchDistance, iDistance);
#if defined(ENABLE_NAVMESH_DISPLAY)
	Navmesh& rCurrent = rFrame.navmesh;
	float fBillboardX = IToX(iNodeX);
	float fBillboardY = JToY(iNodeY);
	rFrame.billboards.Add(rCurrent.uiBillboard,
	{
		.flags = {engine::BillboardFlags::kTypeNone},
		.crc = data::kGltfSpaceshipscenegltfCrc,
		.fSize = 0.1f,
		.fAlpha = 1.0f,
		.fRotation = 0.0f,
		.vecPosition = XMVectorSet(fBillboardX, fBillboardY, gBaseHeight.Get(), 1.0f),
	});
#endif
	return XMVectorSet(IToX(rFrame.f4GlobalArea, iNodeX), JToY(rFrame.f4GlobalArea, iNodeY), gBaseHeight.Get(), 1.0f);
}

bool Navmesh::operator==(const Navmesh& rOther) const
{
	bool bEqual = true;

	for (int64_t j = 0; j < kiGrid; ++j)
	{
		for (int64_t i = 0; i < kiGrid; ++i)
		{
			bEqual &= iLastPlayerX == rOther.iLastPlayerX;
			bEqual &= iLastPlayerY == rOther.iLastPlayerY;
			bEqual &= ppbGridNavigable[j][i] == rOther.ppbGridNavigable[j][i];
		}
	}

	common::BreakOnNotEqual(bEqual);

	return bEqual;
}

} // namespace engine

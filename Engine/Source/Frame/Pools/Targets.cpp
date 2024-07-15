#include "Targets.h"

#include "Frame/Frame.h"

using namespace DirectX;

namespace engine
{

using enum TargetFlags;

void Targets::Interpolate(game::Frame& __restrict rFrame)
{
	Targets& rCurrent = rFrame.targets;

	for (decltype(rCurrent.uiMaxIndex) i = 0; i <= rCurrent.uiMaxIndex; ++i)
	{
		if (!rCurrent.pbUsed[i])
		{
			continue;
		}

		TargetInfo& rTargetInfo = rCurrent.pObjectInfos[i];
		Target& rTarget = rCurrent.pObjects[i];

		if (rTarget.iBillboard > 0)
		{
			rFrame.billboards.Add(rTarget.uiBillboard,
			{
				.flags = {BillboardFlags::kTypeNone},
				.crc = data::kTexturesBC4TargetpngCrc,
				.fSize = rTargetInfo.fBillboardSize,
				.fAlpha = 2.0f,
				.fRotation = 0.0f,
				.fExtra = 0.0f,
				.vecPosition = rTargetInfo.vecPosition,
			});
		}
		else
		{
			rFrame.billboards.Remove(rTarget.uiBillboard);
		}
	}
}

void Targets::Remove(game::Frame& __restrict rFrame, target_t& __restrict ruiIndex, TargetFlags_t flags)
{
	if (ruiIndex == 0)
	{
		return;
	}

	TargetInfo& rTargetInfo = GetInfo(ruiIndex);
	Target& rTarget = Get(ruiIndex);

	if (flags & kDestination)
	{
		rTargetInfo.flags &= kDestination;
	}
	else
	{
		ASSERT(rTarget.uiSubscribers > 0);
		--rTarget.uiSubscribers;
	}

	if (!(rTargetInfo.flags & kDestination) && rTarget.uiSubscribers == 0)
	{
		rFrame.billboards.Remove(rTarget.uiBillboard);
		ObjectPool::Remove(ruiIndex);
	}

	ruiIndex = 0;
}

} // namespace engine

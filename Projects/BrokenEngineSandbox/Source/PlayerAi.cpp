#include "PlayerAi.h"

#include "Frame/Frame.h"
#include "Frame/Player.h"

namespace game
{

using enum PlayerFlags;

void PlayerAi::Update(const Frame& rCurrentFrame, FrameInput& rFrameInput)
{
	const PlayersPostRender& rPlayers = rCurrentFrame.postRender.players;

	for (int64_t i = 1; i < rPlayers.iCount; ++i)
	{
		if (rPlayers.pFlags[i] & kExploding)
		{
			continue;
		}

		mfTimers[i] -= kfDeltaTime;

		if (mfTimers[i] <= 0.0f)
		{
			float fAngle = common::Random<XM_2PI>(mRandomEngine);
			mVecDirections[i] = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(fAngle));
			mfTimers[i] = 1.0f + common::Random<2.0f>(mRandomEngine);
		}

		rFrameInput.playerInputs[i].vecDirection = mVecDirections[i];
		rFrameInput.playerInputs[i].f3Move = XMFLOAT3(XMVectorGetX(mVecDirections[i]), XMVectorGetY(mVecDirections[i]), 0.0f);
		rFrameInput.playerInputs[i].flags.Set(FrameInputHeldFlags::kPrimary);
	}
}

} // namespace game

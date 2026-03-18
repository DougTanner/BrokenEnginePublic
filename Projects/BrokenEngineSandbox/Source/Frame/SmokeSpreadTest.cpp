#include "SmokeSpreadTest.h"

#include "Frame/Frame.h"
#include "Frame/Collections/Spaceships/Spaceships.h"

namespace game
{

using enum PlayerFlags;

constexpr float kfFocusX = 40.0f;
constexpr float kfFocusY = 0.0f;
constexpr float kfPlayerX = 80.0f;
constexpr float kfPlayerY = 0.0f;

void XM_CALLCONV SmokeSpreadTestUpdatePlayer(XMVECTOR& vecVelocity, XMVECTOR& vecWantedDirection, PlayerFlags_t& flags, FXMVECTOR vecPosition)
{
	// Pull toward fixed position and hold
	constexpr float kfCorrectionRate = 10.0f;
	XMVECTOR vecDesired = XMVectorSet(kfPlayerX, kfPlayerY, 0.0f, 0.0f);
	vecVelocity = XMVectorScale(XMVectorSubtract(vecDesired, vecPosition), kfCorrectionRate);

	// Face the spaceship spawn point
	vecWantedDirection = XMVector3Normalize(XMVectorSet(kfFocusX - kfPlayerX, kfFocusY - kfPlayerY, 0.0f, 0.0f));

	// Fire weapons every frame
	flags.Set(kFireBlaster);
	flags.Set(kFireMissile);
}

void SmokeSpreadTestSpawnSpaceship(Frame& rFrame)
{
	// Only spawn from the grid cell that contains the focus point
	XMVECTOR vecFocusPosition = XMVectorSet(kfFocusX, kfFocusY, 0.0f, 0.0f);
	if (!common::InsideArea(vecFocusPosition, rFrame.postRender.vecArea))
	{
		return;
	}

	// Random direction for variety
	float fAngle = common::Random<XM_2PI>(rFrame.postRender.randomEngine);
	XMVECTOR vecDirection = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(fAngle));

	SpaceshipsPostRender::Spawn(rFrame,
	{
		.vecPosition = XMVectorSet(kfFocusX, kfFocusY, engine::gBaseHeight.Get(), 0.0f),
		.vecDirection = vecDirection,
		.alignment = rFrame.postRender.enemyAlignment,
	});
}

} // namespace game

#pragma once

namespace game
{

struct Frame;

class Camera
{
public:

	// DT: TODO Baseheight
	static inline constexpr XMVECTOR kVecMainMenuPosition {29.0f, -81.0f, 0.0f, 1.0f};

	Camera();
	~Camera() = default;

	void Update(const Frame& rFrame);

	common::Timer mRealTime;

	XMVECTOR mVecPosition = kVecMainMenuPosition;
	XMVECTOR mVecEyePosition {};
	XMVECTOR mVecToEyeNormal {};

	float mfShake = 0.0f;
};

} // namespace game

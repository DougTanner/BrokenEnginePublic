#pragma once

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInputHeld;

struct alignas(64) Camera
{
	static constexpr DirectX::XMVECTOR kVecMainMenuPosition {29.0f, -81.0f, 0.0f, 1.0f};

	// Global
	DirectX::XMVECTOR vecPosition = kVecMainMenuPosition;

	// Interpolate
	float fEyeHeight = 150.0f;
	float fEyeRotation = -1.2f;
	float fCameraShake = 0.0f;

	DirectX::XMVECTOR vecOffsetSmoothed {};
	DirectX::XMVECTOR vecEyePosition {};
	DirectX::XMVECTOR vecToEyeNormal {};

	// Post render
	float fEyeHeightVelocity = 0.0f;
	float fEyeRotationVelocity = 0.0f;

	// Utility
	bool operator==(const Camera& rOther) const;
	DirectX::XMVECTOR XM_CALLCONV EyePosition();

	// Update
	static void Global(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, float fDeltaTime);
	static void Interpolate(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, float fDeltaTime);
	static void PostRender(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void Collide(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void Spawn(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void Destroy(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);

	// Render
	static void RenderGlobal(int64_t iCommandBuffer, const Frame& __restrict rFrame);
	static void RenderMain(int64_t iCommandBuffer, const Frame& __restrict rFrame);
};
static_assert(std::is_trivially_copyable_v<Camera>);
inline constexpr int64_t kiCameraVersion = 1 + sizeof(Camera);

} // namespace game

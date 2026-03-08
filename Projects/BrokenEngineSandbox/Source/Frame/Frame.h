#pragma once

#include "Frame/FrameBase.h"
#include "Frame/GridCoord.h"

#include "Input/Input.h"

namespace game
{

// Forward declarations (definitions in individual collection headers)
struct PlayersInterpolate;
struct PlayersPostRender;
struct BlastersInterpolate;
struct BlastersPostRender;
struct MissilesInterpolate;
struct MissilesPostRender;
struct SpaceshipsInterpolate;
struct SpaceshipsPostRender;
struct TargetsInterpolate;
struct TargetsPostRender;

using player_t = engine::id_t<PlayersInterpolate>;
using target_t = engine::id_t<TargetsInterpolate>;

enum class GameFlags : uint64_t
{
	kMainMenu    = 0x00000001,
	kGame        = 0x00000002,
	kDeathScreen = 0x00000008,
};
using GameFlags_t = common::Flags<GameFlags>;

// Set simulation timestep to 32/64/128 fps (kfDeltaTime: 0.03125f/0.015625f/0.0078125f)
inline constexpr std::chrono::nanoseconds kUpdateStepNs = 1'000'000'000ns / 32;
inline constexpr float kfDeltaTime = common::NanosecondsToFloatSeconds<float>(kUpdateStepNs);

struct FrameInterpolate : public engine::FrameInterpolateBase
{
	// Called on Game creation
	static void Register();

#ifdef BT_CLIENT
	// Called during Graphics creation
	static void GraphicsResources();
#endif

	// Interpolate phases
	static void AllocateAndCopy(FrameInterpolate& __restrict rCurrent, const FrameInterpolate& __restrict rPrevious);
	static void Update(FrameInterpolate& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);

#ifdef BT_CLIENT
	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<engine::GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<engine::GridCoord>& rActiveCoords);
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
#endif

	FrameInterpolate();
	~FrameInterpolate();
	FrameInterpolate(FrameInterpolate&&) noexcept;
	FrameInterpolate& operator=(FrameInterpolate&&) noexcept;

	GameFlags_t gameFlags;
	float fSpawnTimer = 0.0f;

	std::unique_ptr<PlayersInterpolate> pPlayers;

	std::unique_ptr<BlastersInterpolate> pBlasters;
	std::unique_ptr<MissilesInterpolate> pMissiles;
	std::unique_ptr<SpaceshipsInterpolate> pSpaceships;
	std::unique_ptr<TargetsInterpolate> pTargets;

	bool operator==(const FrameInterpolate& rOther) const;
	static common::crc_t Crc(const FrameInterpolate& rCurrent);
	static common::crc_t ServerCrc(const FrameInterpolate& rCurrent);
	bool ServerCompare(const FrameInterpolate& rOther) const;
	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);
	void ServerRead(std::istream& rStream);
};

struct TransferRequest
{
	StatusChangeType eType {};
	TransferData data {};
	int64_t iEntityId = 0;
	int8_t iDeltaX = 0;
	int8_t iDeltaY = 0;
};

struct FrameBounds
{
	float fMinX {};
	float fMinY {};
	float fMaxX {};
	float fMaxY {};
};

inline FrameBounds XM_CALLCONV ComputeFrameBounds(FXMVECTOR vecArea)
{
	// vecArea: x=minX, y=maxY, z=maxX, w=minY
	return {
		.fMinX = XMVectorGetX(vecArea),
		.fMinY = XMVectorGetW(vecArea),
		.fMaxX = XMVectorGetZ(vecArea),
		.fMaxY = XMVectorGetY(vecArea),
	};
}

// Compute world-space frame bounds for a given grid coordinate
inline XMVECTOR XM_CALLCONV ComputeFrameArea(FXMVECTOR vecBaseArea, engine::GridCoord coord)
{
	float fWidth = XMVectorGetZ(vecBaseArea) - XMVectorGetX(vecBaseArea);
	float fHeight = XMVectorGetY(vecBaseArea) - XMVectorGetW(vecBaseArea);
	XMVECTOR vecOffset = XMVectorSet(static_cast<float>(coord.x) * fWidth, static_cast<float>(coord.y) * fHeight, static_cast<float>(coord.x) * fWidth, static_cast<float>(coord.y) * fHeight);
	return XMVectorAdd(vecBaseArea, vecOffset);
}

inline constexpr size_t kuiInitialTransferCapacity = 32;

inline bool XM_CALLCONV IsOutOfBounds(const FrameBounds& rBounds, FXMVECTOR vecPosition)
{
	float fPositionX = XMVectorGetX(vecPosition);
	float fPositionY = XMVectorGetY(vecPosition);

	return !(fPositionX > rBounds.fMinX && fPositionX < rBounds.fMaxX &&
	         fPositionY > rBounds.fMinY && fPositionY < rBounds.fMaxY);
}

inline void XM_CALLCONV ComputeTransferDelta(const FrameBounds& rBounds, FXMVECTOR vecPosition, int8_t& rDeltaX, int8_t& rDeltaY)
{
	float fPositionX = XMVectorGetX(vecPosition);
	float fPositionY = XMVectorGetY(vecPosition);
	rDeltaX = static_cast<int8_t>((fPositionX >= rBounds.fMaxX) ? 1 : (fPositionX <= rBounds.fMinX) ? -1 : 0);
	rDeltaY = static_cast<int8_t>((fPositionY >= rBounds.fMaxY) ? 1 : (fPositionY <= rBounds.fMinY) ? -1 : 0);
}

struct FramePostRender : public engine::FramePostRenderBase
{
	FramePostRender();
	~FramePostRender();
	FramePostRender(FramePostRender&&) noexcept;
	FramePostRender& operator=(FramePostRender&&) noexcept;

	// Post render phases
	static void AllocateAndCopy(FramePostRender& __restrict rCurrent, const FramePostRender& __restrict rPrevious);
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void Transfer(Frame& __restrict rFrame);
	static void Destroy(Frame& __restrict rFrame);
	static void Spawn(Frame& __restrict rFrame, const FrameInput& __restrict rFrameInput);

	engine::alignment_t enemyAlignment {};
	engine::alignment_t playerAlignment {};

	std::unique_ptr<PlayersPostRender> pPlayers;

	std::unique_ptr<BlastersPostRender> pBlasters;
	std::unique_ptr<MissilesPostRender> pMissiles;
	std::unique_ptr<SpaceshipsPostRender> pSpaceships;
	std::unique_ptr<TargetsPostRender> pTargets;

	// Transient transfer output buffer (not serialized, not in CRC/equality)
	std::vector<TransferRequest> transferRequests;

	bool operator==(const FramePostRender& rOther) const;
	static common::crc_t Crc(const FramePostRender& rCurrent);
	static common::crc_t ServerCrc(const FramePostRender& rCurrent);
	bool ServerCompare(const FramePostRender& rOther) const;
	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);
	void ServerRead(std::istream& rStream);
};

struct Frame
{
	Frame();
	~Frame();
	Frame(Frame&&) noexcept;
	Frame& operator=(Frame&&) noexcept;

	static constexpr int64_t kiVersion = 15;

	static constexpr int64_t kiIslandCount = 1;
	static constexpr float kpfIslandPositions[kiIslandCount][4] = {{-100.0f, 100.0f, 200.0f, -200.0f}};

	static constexpr float kfBaseAreaMinX = kpfIslandPositions[0][0];
	static constexpr float kfBaseAreaMaxY = kpfIslandPositions[0][1];
	static constexpr float kfBaseAreaMaxX = kpfIslandPositions[0][0] + kpfIslandPositions[0][2];
	static constexpr float kfBaseAreaMinY = kpfIslandPositions[0][1] + kpfIslandPositions[0][3];

	static [[nodiscard]] target_t XM_CALLCONV GetMissileTarget(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecDirection, engine::alignment_t alignment);

	FrameInterpolate interpolate;
	FramePostRender postRender;

	bool operator==(const Frame& rOther) const;
	common::crc_t Crc() const;
	common::crc_t ServerCrc() const;
	bool ServerCompare(const Frame& rOther) const;
	void ServerRead(std::istream& rStream);
};

std::ostream& operator<<(std::ostream& rStream, const Frame& rCurrent);
std::istream& operator>>(std::istream& rStream, Frame& rCurrent);

} // namespace game

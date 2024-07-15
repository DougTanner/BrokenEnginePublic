#pragma once

#include "Frame/UpdateList.h"
#include "Frame/Pools/ObjectPool.h"

namespace game
{

struct Frame;

}

namespace engine
{

enum class BillboardFlags : uint8_t
{
	kOffscreenOnly   = 0x01,
	kOffscreenRotate = 0x02,

	// DT: GAMELOGIC
	kTypeNone  = 0x04,
	kTypeArmor = 0x08,
};
using BillboardFlags_t = common::Flags<BillboardFlags>;

struct BillboardInfo
{
	BillboardFlags_t flags;
	common::crc_t crc = 0;
	float fSize = 0.0f;
	float fAlpha = 1.0f;
	float fRotation = 0.0f;
	float fExtra = 0.0f;
	DirectX::XMVECTOR vecPosition {};

	bool operator==(const BillboardInfo& rOther) const = default;
};
struct Billboard
{
	float fTime = 0.0f;

	bool operator==(const Billboard& rOther) const = default;
};
class Billboards : public UpdateList, public ObjectPool<BillboardInfo, Billboard, billboard_t, kuiMaxBillboards>
{
public:

	static void RenderMain(int64_t iCommandBuffer, const game::Frame& __restrict rFrame);
};
static_assert(std::is_trivially_copyable_v<Billboards>);

inline constexpr int64_t kiBillboardsVersion = 1 + sizeof(Billboards);

}

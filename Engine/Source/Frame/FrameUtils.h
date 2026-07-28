#pragma once

#include "Frame/GridCoord.h"

namespace game
{

struct Frame;
struct FrameInterpolate;
struct FramePostRender;

} // namespace game

namespace engine
{

struct FrameStaticData;

// Decoupled movement: drag decays velocity, acceleration scales down near max speed
// kbBlendVelocityToDirection: when true, blends velocity direction toward vecDirection (airplane-like constraint)
template<bool kbBlendVelocityToDirection = false>
[[nodiscard]] inline XMVECTOR XM_CALLCONV ApplyMovement(FXMVECTOR vecVelocity, FXMVECTOR vecDirection, float fDeltaTime, float fAcceleration, float fDrag, float fMaxSpeed, float fVelocityToDirection = 0.0f)
{
	XMVECTOR vecResult = XMVectorMultiply(XMVectorReplicate(common::ExponentialDecay(fDrag, fDeltaTime)), vecVelocity);

	float fSpeed = XMVectorGetX(XMVector3Length(vecResult));
	float fAccelScale = 1.0f - std::min(fSpeed / fMaxSpeed, 1.0f);
	vecResult = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * fAcceleration * fAccelScale), vecDirection, vecResult);

	if constexpr (kbBlendVelocityToDirection)
	{
		float fDecay = common::ExponentialDecay(fVelocityToDirection, fDeltaTime);
		XMVECTOR vecVelocityComponent = XMVectorMultiply(XMVectorReplicate(fDecay), XMVector3Normalize(vecResult));
		XMVECTOR vecDirectionComponent = XMVectorMultiply(XMVectorReplicate(1.0f - fDecay), vecDirection);
		vecResult = XMVectorMultiply(XMVector3Length(vecResult), XMVector3Normalize(XMVectorAdd(vecVelocityComponent, vecDirectionComponent)));
	}

	return vecResult;
}

// Type list for fold expression iteration
template<typename... TS>
struct TypeList {};

// Convert std::tuple<T1&, T2&, ...> to TypeList<T1, T2, ...>
// Strips references from tuple element types
template<typename TUPLE>
struct TupleToTypeList;

template<typename... TS>
struct TupleToTypeList<std::tuple<TS...>>
{
	using type = TypeList<std::remove_reference_t<TS>...>;
};

template<typename TUPLE>
using TupleToTypeList_t = typename TupleToTypeList<TUPLE>::type;

// ForEach helpers for collection iteration via fold expressions
template<typename... TS>
void ForEachInterpolateUpdate(TypeList<TS...>, game::FrameInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame)
{
	([&]
	{
		static_assert(requires { TS::Update(rCurrent, rPreviousFrame); });
		TS::Update(rCurrent, rPreviousFrame);
	}(), ...);
}

template<typename... TS>
void ForEachInterpolateRender(TypeList<TS...>, const game::FrameInterpolate& __restrict rCurrent, int64_t iCommandBuffer)
{
	([&]
	{
		if constexpr (!requires { TS::kbManualRender; } && requires { TS::Render(rCurrent, iCommandBuffer); })
		{
			TS::Render(rCurrent, iCommandBuffer);
		}
	}(), ...);
}

template<typename... TS>
void ForEachBeginRender(TypeList<TS...>, int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords)
{
	([&]
	{
		if constexpr (requires { TS::BeginRender(iCommandBuffer, rRenderInterpolates, rActiveCoords); })
		{
			TS::BeginRender(iCommandBuffer, rRenderInterpolates, rActiveCoords);
		}
	}(), ...);
}

template<typename... TS>
constexpr void ForEachEndRender(TypeList<TS...>, int64_t iCommandBuffer)
{
	([&]
	{
		if constexpr (requires { TS::EndRender(iCommandBuffer); })
		{
			TS::EndRender(iCommandBuffer);
		}
	}(), ...);
}

template<typename... TS>
constexpr void ForEachRegister(TypeList<TS...>)
{
	([&]
	{
		if constexpr (requires { TS::Register(); })
		{
			TS::Register();
		}
	}(), ...);
}

template<typename... TS>
constexpr void ForEachGraphicsResources(TypeList<TS...>)
{
	([&]
	{
		if constexpr (requires { TS::GraphicsResources(); })
		{
			TS::GraphicsResources();
		}
	}(), ...);
}

template<typename... TS>
void ForEachPostRenderUpdate(TypeList<TS...>, game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData)
{
	([&]
	{
		static_assert(requires { TS::Update(rFrame, rPreviousFrame, rStaticData); });
		TS::Update(rFrame, rPreviousFrame, rStaticData);
	}(), ...);
}

template<typename... TS>
void ForEachPostRenderPreCollision(TypeList<TS...>, game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData)
{
	([&]
	{
		if constexpr (requires { TS::PreCollision(rFrame, rPreviousFrame, rStaticData); })
		{
			TS::PreCollision(rFrame, rPreviousFrame, rStaticData);
		}
	}(), ...);
}

template<typename... TS>
void ForEachPostRenderPostCollision(TypeList<TS...>, game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData)
{
	([&]
	{
		if constexpr (requires { TS::PostCollision(rFrame, rPreviousFrame, rStaticData); })
		{
			TS::PostCollision(rFrame, rPreviousFrame, rStaticData);
		}
	}(), ...);
}

template<typename... TS>
void ForEachPostRenderAreaDamage(TypeList<TS...>, game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData)
{
	([&]
	{
		if constexpr (requires { TS::AreaDamage(rFrame, rPreviousFrame, rStaticData); })
		{
			TS::AreaDamage(rFrame, rPreviousFrame, rStaticData);
		}
	}(), ...);
}

template<typename... TS>
void ForEachPostRenderTransfer(TypeList<TS...>, game::Frame& __restrict rFrame, const FrameStaticData& rStaticData)
{
	([&]
	{
		if constexpr (requires { TS::Transfer(rFrame, rStaticData); })
		{
			TS::Transfer(rFrame, rStaticData);
		}
	}(), ...);
}

template<typename... TS>
void ForEachPostRenderDestroy(TypeList<TS...>, game::Frame& __restrict rFrame, const FrameStaticData& rStaticData)
{
	([&]
	{
		if constexpr (requires { TS::Destroy(rFrame, rStaticData); })
		{
			TS::Destroy(rFrame, rStaticData);
		}
	}(), ...);
}

template<typename... TS>
void ForEachPostRenderSpawn(TypeList<TS...>, game::Frame& __restrict rFrame, const FrameStaticData& rStaticData)
{
	([&]
	{
		if constexpr (requires { TS::Spawn(rFrame, rStaticData); })
		{
			TS::Spawn(rFrame, rStaticData);
		}
	}(), ...);
}

// AllocateAndCopy helper using tuple and index sequence
template<typename TUPLE_CURRENT, typename TUPLE_PREVIOUS, size_t... INDICES>
void AllocateAndCopyCollections(TUPLE_CURRENT&& current, TUPLE_PREVIOUS&& previous, std::index_sequence<INDICES...>)
{
	(std::remove_reference_t<std::tuple_element_t<INDICES, std::remove_cvref_t<TUPLE_CURRENT>>>::AllocateAndCopy(
		std::get<INDICES>(current), std::get<INDICES>(previous)), ...);
}

// LogDifferences helper using tuple and index sequence; left-to-right, never short-circuits (every collection logs)
template<typename TUPLE_CURRENT, typename TUPLE_OTHER, size_t... INDICES>
bool LogDifferencesCollections(TUPLE_CURRENT&& current, TUPLE_OTHER&& other, std::index_sequence<INDICES...>)
{
	bool bEqual = true;
	((bEqual &= std::get<INDICES>(current).LogDifferences(std::get<INDICES>(other))), ...);
	return bEqual;
}

} // namespace engine

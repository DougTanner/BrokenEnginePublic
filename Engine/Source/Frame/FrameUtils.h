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
template<typename Tuple>
struct TupleToTypeList;

template<typename... Ts>
struct TupleToTypeList<std::tuple<Ts...>>
{
	using type = TypeList<std::remove_reference_t<Ts>...>;
};

template<typename Tuple>
using TupleToTypeList_t = typename TupleToTypeList<Tuple>::type;

// ForEach helpers for collection iteration via fold expressions
template<typename... TS>
void ForEachInterpolateUpdate(TypeList<TS...>, game::FrameInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame)
{
	(TS::Update(rCurrent, rPreviousFrame), ...);
}

template<typename... TS>
void ForEachInterpolateRender(TypeList<TS...>, const game::FrameInterpolate& __restrict rCurrent, int64_t iCommandBuffer)
{
	(TS::Render(rCurrent, iCommandBuffer), ...);
}

template<typename... TS>
void ForEachBeginRender(TypeList<TS...>, int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords)
{
	(TS::BeginRender(iCommandBuffer, rRenderInterpolates, rActiveCoords), ...);
}

template<typename... TS>
void ForEachEndRender(TypeList<TS...>, int64_t iCommandBuffer)
{
	(TS::EndRender(iCommandBuffer), ...);
}

template<typename... TS>
void ForEachRegister(TypeList<TS...>)
{
	(TS::Register(), ...);
}

template<typename... TS>
void ForEachGraphicsResources(TypeList<TS...>)
{
	(TS::GraphicsResources(), ...);
}

template<typename... TS>
void ForEachPostRenderUpdate(TypeList<TS...>, game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData)
{
	(TS::Update(rFrame, rPreviousFrame, rStaticData), ...);
}

template<typename... TS>
void ForEachPostRenderPreCollision(TypeList<TS...>, game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData)
{
	(TS::PreCollision(rFrame, rPreviousFrame, rStaticData), ...);
}

template<typename... TS>
void ForEachPostRenderPostCollision(TypeList<TS...>, game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData)
{
	(TS::PostCollision(rFrame, rPreviousFrame, rStaticData), ...);
}

template<typename... TS>
void ForEachPostRenderAreaDamage(TypeList<TS...>, game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData)
{
	(TS::AreaDamage(rFrame, rPreviousFrame, rStaticData), ...);
}

template<typename... TS>
void ForEachPostRenderTransfer(TypeList<TS...>, game::Frame& __restrict rFrame, const FrameStaticData& rStaticData)
{
	(TS::Transfer(rFrame, rStaticData), ...);
}

template<typename... TS>
void ForEachPostRenderDestroy(TypeList<TS...>, game::Frame& __restrict rFrame, const FrameStaticData& rStaticData)
{
	(TS::Destroy(rFrame, rStaticData), ...);
}

template<typename... TS>
void ForEachPostRenderSpawn(TypeList<TS...>, game::Frame& __restrict rFrame, const FrameStaticData& rStaticData)
{
	(TS::Spawn(rFrame, rStaticData), ...);
}

// AllocateAndCopy helper using tuple and index sequence
template<typename TTupleCurrent, typename TTuplePrevious, size_t... Is>
void AllocateAndCopyCollections(TTupleCurrent&& current, TTuplePrevious&& previous, std::index_sequence<Is...>)
{
	(std::remove_reference_t<std::tuple_element_t<Is, std::remove_cvref_t<TTupleCurrent>>>::AllocateAndCopy(
		std::get<Is>(current), std::get<Is>(previous)), ...);
}

// LogDifferences helper using tuple and index sequence; left-to-right, never short-circuits (every collection logs)
template<typename TTupleCurrent, typename TTupleOther, size_t... Is>
bool LogDifferencesCollections(TTupleCurrent&& current, TTupleOther&& other, std::index_sequence<Is...>)
{
	bool bEqual = true;
	((bEqual &= std::get<Is>(current).LogDifferences(std::get<Is>(other))), ...);
	return bEqual;
}

} // namespace engine

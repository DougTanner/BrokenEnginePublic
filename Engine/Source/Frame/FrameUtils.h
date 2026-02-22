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
void ForEachPostRenderUpdate(TypeList<TS...>, game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame)
{
	(TS::Update(rFrame, rPreviousFrame), ...);
}

template<typename... TS>
void ForEachPostRenderPreCollision(TypeList<TS...>, game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame)
{
	(TS::PreCollision(rFrame, rPreviousFrame), ...);
}

template<typename... TS>
void ForEachPostRenderPostCollision(TypeList<TS...>, game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame)
{
	(TS::PostCollision(rFrame, rPreviousFrame), ...);
}

template<typename... TS>
void ForEachPostRenderAreaDamage(TypeList<TS...>, game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame)
{
	(TS::AreaDamage(rFrame, rPreviousFrame), ...);
}

template<typename... TS>
void ForEachPostRenderTransfer(TypeList<TS...>, game::Frame& __restrict rFrame)
{
	(TS::Transfer(rFrame), ...);
}

template<typename... TS>
void ForEachPostRenderDestroy(TypeList<TS...>, game::Frame& __restrict rFrame)
{
	(TS::Destroy(rFrame), ...);
}

template<typename... TS>
void ForEachPostRenderSpawn(TypeList<TS...>, game::Frame& __restrict rFrame)
{
	(TS::Spawn(rFrame), ...);
}

// AllocateAndCopy helper using tuple and index sequence
template<typename TTupleCurrent, typename TTuplePrevious, size_t... Is>
void AllocateAndCopyCollections(TTupleCurrent&& current, TTuplePrevious&& previous, std::index_sequence<Is...>)
{
	(std::remove_reference_t<std::tuple_element_t<Is, std::remove_cvref_t<TTupleCurrent>>>::AllocateAndCopy(
		std::get<Is>(current), std::get<Is>(previous)), ...);
}

// Collection comparison helper using index sequence
template<typename TTuple1, typename TTuple2, size_t... Is>
bool CompareCollections(const TTuple1& t1, const TTuple2& t2, std::index_sequence<Is...>)
{
	bool bEqual = true;
	((bEqual &= common::BreakOnNotEqual(std::get<Is>(t1), std::get<Is>(t2))), ...);
	return bEqual;
}

} // namespace engine

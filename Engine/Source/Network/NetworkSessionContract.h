#pragma once

namespace engine
{

template <typename T>
concept NetworkSessionContractType = requires(typename T::GamePacket ePacket)
{
	typename T::Frame;
	typename T::StatusChange;
	{ T::GetFrameVersion() } -> std::convertible_to<int64_t>;
	{ T::kTickDuration } -> std::convertible_to<std::chrono::nanoseconds>;
	{ T::kiCoordSlots } -> std::convertible_to<int64_t>;
	{ T::kbDebugFrames } -> std::convertible_to<bool>;
	{ T::GetClientPacketContract(ePacket) } -> std::same_as<ClientPacketContract>;
	{ T::WriteFrame(std::declval<std::ostream&>(), std::declval<const typename T::Frame&>()) } -> std::same_as<void>;
	{ T::ReadFrame(std::declval<std::istream&>(), std::declval<typename T::Frame&>()) } -> std::same_as<void>;
	{ T::CompressStatusChanges(std::declval<const typename T::StatusChange*>(), 0, nullptr, 0) } -> std::same_as<int64_t>;
	{ T::DecompressStatusChanges(nullptr, 0, std::declval<typename T::StatusChange*>(), 0) } -> std::same_as<int64_t>;
};

static_assert(NetworkSessionContractType<game::NetworkSessionContract>);

} // namespace engine

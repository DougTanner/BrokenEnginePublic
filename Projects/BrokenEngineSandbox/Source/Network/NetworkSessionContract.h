#pragma once

#include "Network/NetworkProtocol.h"
#include "Network/NetworkSerialization.h"
#include "Network/GamePacketType.h"

namespace game
{

inline constexpr int64_t kiDesiredCoordSlots = 16; // 9 visible subscriptions + 6 sticky + 1 spare

// Canonical compile-time contract consumed by engine-owned session runtimes. A second
// grid/frame game supplies the same shape while retaining its own concrete payloads.
struct NetworkSessionContract
{
	using Frame = game::Frame;
	using StatusChange = game::StatusChange;
	using GamePacket = game::GamePacketType;

	static int64_t GetFrameVersion()
	{
		return Frame::kiVersion;
	}
	static constexpr std::chrono::nanoseconds kTickDuration = kTickNs;
	static constexpr int64_t kiCoordSlots = kiDesiredCoordSlots;
	static constexpr bool kbDebugFrames = kbDesyncDebugFrames;

	static constexpr engine::ClientPacketContract GetClientPacketContract(GamePacket eType)
	{
		return GetGamePacketContract(eType);
	}

	static void WriteFrame(std::ostream& rStream, const Frame& rFrame)
	{
		rStream << rFrame;
	}
	static void ReadFrame(std::istream& rStream, Frame& rFrame)
	{
		rFrame.ServerRead(rStream);
	}
	static int64_t CompressStatusChanges(const StatusChange* pChanges, int64_t iCount, void* pDestination, int64_t iCapacity)
	{
		return engine::CompressStatusChangeBatch(pChanges, iCount, pDestination, iCapacity);
	}
	static int64_t DecompressStatusChanges(const void* pSource, int64_t iSize, StatusChange* pDestination, int64_t iCapacity)
	{
		return engine::DecompressStatusChangeBatch(pSource, iSize, pDestination, iCapacity);
	}
};

} // namespace game

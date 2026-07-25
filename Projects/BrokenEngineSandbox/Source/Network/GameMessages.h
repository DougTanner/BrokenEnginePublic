#pragma once

#include "Fleet.h"
#include "Network/PlayerEvents.h"

namespace game::GameMessages
{

struct AssignPlayerMessage
{
	static constexpr int64_t kiSize = sizeof(int64_t) + engine::NetworkMessages::kiGridCoordSize;
	static_assert(kiSize == 16);

	int64_t iGlobalPlayerId = 0;
	engine::GridCoord coord {};

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, AssignPlayerMessage& rMessage)
	{
		rVisitor.Field(rMessage.iGlobalPlayerId);
		rVisitor.Field(rMessage.coord);
	}
};

struct PlayerStateMessage
{
	static constexpr int64_t kiSize = sizeof(uint8_t) + sizeof(int64_t) + engine::NetworkMessages::kiGridCoordSize;
	static_assert(kiSize == 17);

	uint8_t uiWireType = 0;
	int64_t iGlobalPlayerId = 0;
	engine::GridCoord coord {};

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, PlayerStateMessage& rMessage)
	{
		rVisitor.Field(rMessage.uiWireType);
		rVisitor.Field(rMessage.iGlobalPlayerId);
		rVisitor.Field(rMessage.coord);
	}
};

struct PlayerStateDescriptor
{
	PlayerStateWireType eWireType {};
	PlayerEventType eEventType {};
	const char* pcName = "";
};

inline constexpr PlayerStateDescriptor kpPlayerStateDescriptors[] =
{
	{.eWireType = PlayerStateWireType::kSpawned, .eEventType = PlayerEventType::kSpawned, .pcName = "Spawned"},
	{.eWireType = PlayerStateWireType::kChangedFrame, .eEventType = PlayerEventType::kChangedFrame, .pcName = "ChangedFrame"},
	{.eWireType = PlayerStateWireType::kDied, .eEventType = PlayerEventType::kDied, .pcName = "Died"},
};
static_assert(std::size(kpPlayerStateDescriptors) == static_cast<size_t>(PlayerStateWireType::kCount));

inline constexpr const PlayerStateDescriptor& GetPlayerStateDescriptor(PlayerStateWireType eWireType)
{
	return kpPlayerStateDescriptors[static_cast<size_t>(eWireType)];
}

inline constexpr const PlayerStateDescriptor* FindPlayerStateDescriptor(uint8_t uiWireType)
{
	for (const PlayerStateDescriptor& rDescriptor : kpPlayerStateDescriptors)
	{
		if (static_cast<uint8_t>(rDescriptor.eWireType) == uiWireType)
		{
			return &rDescriptor;
		}
	}
	return nullptr;
}

struct FleetSyncMessage
{
	static constexpr int64_t kiFleetCountSize = sizeof(int64_t);
	static constexpr int64_t kiFleetHeaderSize = sizeof(uint64_t) + sizeof(uint64_t) + sizeof(int64_t) + sizeof(int64_t) + sizeof(float);
	static constexpr int64_t kiFleetMemberSize = sizeof(int64_t) + sizeof(uint8_t);

	static_assert(kiFleetCountSize == 8);
	static_assert(kiFleetHeaderSize == 36);
	static_assert(kiFleetMemberSize == 9);

	class FleetWriter
	{
	public:

		explicit FleetWriter(common::Workbuffer& rWorkbuffer)
		: mrWorkbuffer(rWorkbuffer)
		{
		}

		void Field(const uint64_t& rValue) const
		{
			mrWorkbuffer.PushBack<uint64_t>(rValue);
		}

		void Field(const int64_t& rValue) const
		{
			mrWorkbuffer.PushBack<int64_t>(rValue);
		}

		void Field(const uint8_t& rValue) const
		{
			mrWorkbuffer.PushBack<uint8_t>(rValue);
		}

		void MemberCount(const int64_t& rValue) const
		{
			Field(rValue);
		}

		void Alive(const bool& bValue) const
		{
			Field(static_cast<uint8_t>(bValue ? 1 : 0));
		}

		void Float(const float& rValue) const
		{
			mrWorkbuffer.PushBack<float>(rValue);
		}

	private:

		common::Workbuffer& mrWorkbuffer;
	};

	class FleetReader
	{
	public:

		explicit FleetReader(engine::BoundedCursor& rCursor)
		: mrCursor(rCursor)
		{
		}

		void Field(uint64_t& rValue) const
		{
			rValue = engine::ReadUint64(mrCursor.pCursor);
		}

		void Field(int64_t& rValue) const
		{
			rValue = engine::ReadInt64(mrCursor.pCursor);
		}

		void Field(uint8_t& rValue) const
		{
			rValue = engine::ReadUint8(mrCursor.pCursor);
		}

		void MemberCount(int64_t& rValue) const
		{
			Field(rValue);
		}

		void Alive(bool& rValue) const
		{
			rValue = engine::ReadUint8(mrCursor.pCursor) != 0;
		}

		void Float(float& rValue) const
		{
			rValue = engine::ReadFloat(mrCursor.pCursor);
		}

	private:

		engine::BoundedCursor& mrCursor;
	};

	template <typename TVisitor, typename TFleet>
	static void VisitFleetHeader(TVisitor& rVisitor, TFleet& rFleet, int64_t& riMemberCount)
	{
		rVisitor.Field(rFleet.guid.uiHigh);
		rVisitor.Field(rFleet.guid.uiLow);
		rVisitor.MemberCount(riMemberCount);
		rVisitor.Field(rFleet.iFlagshipIndex);
		rVisitor.Float(rFleet.fNavigationDelay);
	}

	template <typename TVisitor, typename TFleetMember>
	static void VisitFleetMember(TVisitor& rVisitor, TFleetMember& rMember)
	{
		rVisitor.Field(rMember.globalPlayerId.iValue);
		rVisitor.Alive(rMember.bAlive);
	}

	static void WritePayload(common::Workbuffer& rWorkbuffer, const std::vector<Fleet>& rFleets)
	{
		int64_t iExpectedSize = rWorkbuffer.Count<uint8_t>() + kiFleetCountSize;
		rWorkbuffer.PushBack<int64_t>(std::ssize(rFleets));
		FleetWriter writer(rWorkbuffer);
		for (const Fleet& rFleet : rFleets)
		{
			int64_t iHeaderStart = rWorkbuffer.Count<uint8_t>();
			int64_t iMemberCount = std::ssize(rFleet.members);
			VisitFleetHeader(writer, rFleet, iMemberCount);
			ASSERT(rWorkbuffer.Count<uint8_t>() == iHeaderStart + kiFleetHeaderSize);
			iExpectedSize += kiFleetHeaderSize;
			for (const FleetMember& rMember : rFleet.members)
			{
				int64_t iMemberStart = rWorkbuffer.Count<uint8_t>();
				VisitFleetMember(writer, rMember);
				ASSERT(rWorkbuffer.Count<uint8_t>() == iMemberStart + kiFleetMemberSize);
				iExpectedSize += kiFleetMemberSize;
			}
		}
		ASSERT(rWorkbuffer.Count<uint8_t>() == iExpectedSize);
	}

	static bool ReadPayload(const std::vector<uint8_t>& rPayload, std::vector<Fleet>& rOutFleets)
	{
		engine::BoundedCursor cursor {rPayload.data(), rPayload.data() + rPayload.size()};
		if (!cursor.Has(kiFleetCountSize))
		{
			return false;
		}

		int64_t iFleetCount = engine::ReadInt64(cursor.pCursor);
		// Divide avoids hostile-count multiplication overflow.
		if (iFleetCount < 0 || iFleetCount > cursor.Remaining() / kiFleetHeaderSize)
		{
			return false;
		}

		rOutFleets.resize(static_cast<size_t>(iFleetCount));
		FleetReader reader(cursor);
		for (int64_t i = 0; i < iFleetCount; ++i)
		{
			// Earlier fleets consume payload, so recheck before each header.
			if (!cursor.Has(kiFleetHeaderSize))
			{
				return false;
			}

			Fleet& rFleet = rOutFleets[static_cast<size_t>(i)];
			int64_t iMemberCount = 0;
			VisitFleetHeader(reader, rFleet, iMemberCount);
			if (iMemberCount < 0 || iMemberCount > cursor.Remaining() / kiFleetMemberSize)
			{
				return false;
			}
			if (rFleet.iFlagshipIndex < 0 ||
				(iMemberCount == 0 && rFleet.iFlagshipIndex != 0) ||
				(iMemberCount > 0 && rFleet.iFlagshipIndex >= iMemberCount))
			{
				return false;
			}

			rFleet.members.resize(static_cast<size_t>(iMemberCount));
			for (int64_t j = 0; j < iMemberCount; ++j)
			{
				VisitFleetMember(reader, rFleet.members[static_cast<size_t>(j)]);
			}
		}

		return cursor.Remaining() == 0;
	}
};

} // namespace game::GameMessages

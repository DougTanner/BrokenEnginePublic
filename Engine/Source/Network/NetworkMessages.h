#pragma once

#include "Network/NetworkCursor.h"

namespace engine::NetworkMessages
{

inline constexpr int64_t kiPacketTypeSize = sizeof(uint8_t);
inline constexpr int64_t kiGridCoordSize = sizeof(int32_t) * 2;
inline constexpr int64_t kiGuidSize = sizeof(uint64_t) * 2;

struct PacketPayload
{
	const uint8_t* pData = nullptr;
	int32_t iSize = 0;
};

class MessageWriter
{
public:

	explicit MessageWriter(common::Workbuffer& rWorkbuffer)
	: mrWorkbuffer(rWorkbuffer)
	{
	}

	void Type(PacketType eType) const
	{
		mrWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(eType));
	}

	void Field(const uint8_t& rValue) const { mrWorkbuffer.PushBack<uint8_t>(rValue); }
	void Field(const uint16_t& rValue) const { mrWorkbuffer.PushBack<uint16_t>(rValue); }
	void Field(const int32_t& rValue) const { mrWorkbuffer.PushBack<int32_t>(rValue); }
	void Field(const uint32_t& rValue) const { mrWorkbuffer.PushBack<uint32_t>(rValue); }
	void Field(const int64_t& rValue) const { mrWorkbuffer.PushBack<int64_t>(rValue); }
	void Field(const uint64_t& rValue) const { mrWorkbuffer.PushBack<uint64_t>(rValue); }

	void Field(const GridCoord& rCoord) const
	{
		WriteGridCoord(mrWorkbuffer, rCoord);
	}

	void Payload(const PacketPayload& rPayload) const
	{
		if (rPayload.iSize > 0)
		{
			mrWorkbuffer.Append(std::string_view(reinterpret_cast<const char*>(rPayload.pData), rPayload.iSize));
		}
	}

	void NullTerminatedString(const std::string_view& rValue) const
	{
		mrWorkbuffer.Append(rValue);
		mrWorkbuffer.PushBack<uint8_t>(0);
	}

	void OptionalGuid(const ClientGuid& rGuid, const bool& bHasGuid) const
	{
		if (bHasGuid)
		{
			Field(rGuid.uiHigh);
			Field(rGuid.uiLow);
		}
	}

	void ConnectionResponseTail(const uint8_t& uiAccepted, const ClientGuid& rGuid, const bool& bHasGuid, const std::string_view& rRejectionMessage) const
	{
		if (uiAccepted != 0 && bHasGuid)
		{
			Field(rGuid.uiHigh);
			Field(rGuid.uiLow);
		}
		else if (uiAccepted == 0)
		{
			mrWorkbuffer.Append(rRejectionMessage);
		}
	}

	void Invalidate() const
	{
	}

private:

	common::Workbuffer& mrWorkbuffer;
};

class MessageReader
{
public:

	MessageReader(std::span<const uint8_t> packetData)
	: mCursor {packetData.data(), packetData.data() + packetData.size()}
	{
	}

	bool IsValid() const
	{
		return mbValid;
	}

	void Type(PacketType eExpected)
	{
		if (!mCursor.Has(kiPacketTypeSize) || ReadUint8(mCursor.pCursor) != static_cast<uint8_t>(eExpected))
		{
			mbValid = false;
		}
	}

	void Field(uint8_t& rValue) { Read(rValue, sizeof(rValue), ReadUint8); }
	void Field(uint16_t& rValue) { Read(rValue, sizeof(rValue), ReadUint16); }
	void Field(int32_t& rValue) { Read(rValue, sizeof(rValue), ReadInt32); }
	void Field(uint32_t& rValue) { Read(rValue, sizeof(rValue), ReadUint32); }
	void Field(int64_t& rValue) { Read(rValue, sizeof(rValue), ReadInt64); }
	void Field(uint64_t& rValue) { Read(rValue, sizeof(rValue), ReadUint64); }

	void Field(GridCoord& rCoord)
	{
		if (!mCursor.Has(kiGridCoordSize))
		{
			mbValid = false;
			return;
		}
		rCoord = ReadGridCoord(mCursor.pCursor);
	}

	void Payload(PacketPayload& rPayload)
	{
		if (rPayload.iSize < 0 || !mCursor.Has(rPayload.iSize))
		{
			mbValid = false;
			return;
		}
		rPayload.pData = mCursor.pCursor;
		mCursor.pCursor += rPayload.iSize;
	}

	void NullTerminatedString(std::string_view& rValue)
	{
		if (!mbValid)
		{
			return;
		}

		const uint8_t* pStart = mCursor.pCursor;
		while (mCursor.pCursor < mCursor.pEnd && *mCursor.pCursor != 0)
		{
			++mCursor.pCursor;
		}
		rValue = std::string_view(reinterpret_cast<const char*>(pStart), static_cast<size_t>(mCursor.pCursor - pStart));
		if (mCursor.pCursor < mCursor.pEnd)
		{
			++mCursor.pCursor;
		}
	}

	void OptionalGuid(ClientGuid& rGuid, bool& bHasGuid)
	{
		bHasGuid = false;
		if (!mbValid || !mCursor.Has(kiGuidSize))
		{
			return;
		}
		Field(rGuid.uiHigh);
		Field(rGuid.uiLow);
		bHasGuid = mbValid;
	}

	void ConnectionResponseTail(uint8_t& uiAccepted, ClientGuid& rGuid, bool& bHasGuid, std::string_view& rRejectionMessage)
	{
		bHasGuid = false;
		rRejectionMessage = {};
		if (!mbValid)
		{
			return;
		}

		if (uiAccepted != 0)
		{
			OptionalGuid(rGuid, bHasGuid);
			return;
		}

		rRejectionMessage = std::string_view(reinterpret_cast<const char*>(mCursor.pCursor), static_cast<size_t>(mCursor.Remaining()));
		mCursor.pCursor = mCursor.pEnd;
	}

	void Invalidate()
	{
		mbValid = false;
	}

private:

	template <typename TValue, typename TRead>
	void Read(TValue& rValue, int64_t iSize, TRead pfnRead)
	{
		if (!mbValid || !mCursor.Has(iSize))
		{
			mbValid = false;
			return;
		}
		rValue = pfnRead(mCursor.pCursor);
	}

	BoundedCursor mCursor;
	bool mbValid = true;
};

class CoordUpdateTickReader
{
public:

	CoordUpdateTickReader(std::span<const uint8_t> packetData)
	: mCursor {packetData.data(), packetData.data() + packetData.size()}
	{
	}

	bool HasTick() const
	{
		return mbValid && mbHasTick;
	}

	int64_t GetTick() const
	{
		return miTick;
	}

	void Type(PacketType eExpected)
	{
		if (!mCursor.Has(kiPacketTypeSize) || ReadUint8(mCursor.pCursor) != static_cast<uint8_t>(eExpected))
		{
			mbValid = false;
		}
	}

	void Field(uint8_t&) { Skip(sizeof(uint8_t)); }
	void Field(uint16_t&) { Skip(sizeof(uint16_t)); }
	void Field(int32_t&) { Skip(sizeof(int32_t)); }
	void Field(uint32_t&) { Skip(sizeof(uint32_t)); }
	void Field(uint64_t&) { Skip(sizeof(uint64_t)); }
	void Field(GridCoord&) { Skip(kiGridCoordSize); }

	void Field(int64_t& rValue)
	{
		if (mbHasTick)
		{
			return;
		}
		if (!mCursor.Has(sizeof(int64_t)))
		{
			mbValid = false;
			return;
		}
		rValue = ReadInt64(mCursor.pCursor);
		miTick = rValue;
		mbHasTick = true;
	}

	void Payload(PacketPayload&) { }
	void NullTerminatedString(std::string_view&) { }
	void OptionalGuid(ClientGuid&, bool&) { }
	void ConnectionResponseTail(uint8_t&, ClientGuid&, bool&, std::string_view&) { }
	void Invalidate() { mbValid = false; }

private:

	void Skip(int64_t iSize)
	{
		if (mbHasTick)
		{
			return;
		}
		if (!mbValid || !mCursor.Has(iSize))
		{
			mbValid = false;
			return;
		}
		mCursor.pCursor += iSize;
	}

	BoundedCursor mCursor;
	int64_t miTick = 0;
	bool mbValid = true;
	bool mbHasTick = false;
};

template <typename TMessage>
inline void Write(common::Workbuffer& rWorkbuffer, TMessage& rMessage)
{
	MessageWriter writer {rWorkbuffer};
	TMessage::Visit(writer, rMessage);
}

template <typename TMessage>
inline bool Read(std::span<const uint8_t> packetData, TMessage& rMessage)
{
	MessageReader reader {packetData};
	TMessage::Visit(reader, rMessage);
	return reader.IsValid();
}

struct AckStreamEntry
{
	static constexpr int64_t kiSize = sizeof(uint8_t) + sizeof(uint16_t) + sizeof(int64_t) + sizeof(uint64_t) + sizeof(uint64_t);

	uint8_t uiSlotIndex = 0;
	uint16_t uiEpoch = 0;
	int64_t iAckFloor = -1;
	uint64_t uiReceivedBitfieldLow = 0;
	uint64_t uiReceivedBitfieldHigh = 0;

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, AckStreamEntry& rMessage)
	{
		rVisitor.Field(rMessage.uiSlotIndex);
		rVisitor.Field(rMessage.uiEpoch);
		rVisitor.Field(rMessage.iAckFloor);
		rVisitor.Field(rMessage.uiReceivedBitfieldLow);
		rVisitor.Field(rMessage.uiReceivedBitfieldHigh);
	}
};

struct ClientAckStreamMessage
{
	static constexpr PacketType keType = PacketType::kClientAckStream;
	static constexpr int64_t kiMaxSlotCount = 64;
	static constexpr int64_t kiFixedSize = kiPacketTypeSize + sizeof(uint8_t) + sizeof(int64_t);

	uint8_t uiSlotCount = 0;
	AckStreamEntry* pEntries = nullptr;
	int64_t iEntryCapacity = 0;
	int64_t iTimestampNs = 0;

	static constexpr int64_t GetSize(int64_t iSlotCount)
	{
		return kiFixedSize + iSlotCount * AckStreamEntry::kiSize;
	}

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ClientAckStreamMessage& rMessage)
	{
		rVisitor.Type(keType);
		rVisitor.Field(rMessage.uiSlotCount);
		if (rMessage.uiSlotCount > rMessage.iEntryCapacity)
		{
			rVisitor.Invalidate();
			return;
		}
		for (uint8_t i = 0; i < rMessage.uiSlotCount; ++i)
		{
			AckStreamEntry::Visit(rVisitor, rMessage.pEntries[i]);
		}
		rVisitor.Field(rMessage.iTimestampNs);
	}
};

struct ClientDesyncReportMessage
{
	static constexpr PacketType keType = PacketType::kClientDesyncReport;
	static constexpr int64_t kiFixedSize = kiPacketTypeSize + sizeof(int64_t) + kiGridCoordSize + sizeof(uint64_t) + sizeof(uint64_t);

	int64_t iTick = 0;
	GridCoord coord {};
	uint64_t uiExpectedCrc = 0;
	uint64_t uiActualCrc = 0;

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ClientDesyncReportMessage& rMessage)
	{
		rVisitor.Type(keType);
		rVisitor.Field(rMessage.iTick);
		rVisitor.Field(rMessage.coord);
		rVisitor.Field(rMessage.uiExpectedCrc);
		rVisitor.Field(rMessage.uiActualCrc);
	}
};

struct ClientDebugFrameRequestMessage
{
	static constexpr PacketType keType = PacketType::kClientDebugFrameRequest;
	static constexpr int64_t kiFixedSize = kiPacketTypeSize + sizeof(int64_t) + kiGridCoordSize;

	int64_t iTick = 0;
	GridCoord coord {};

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ClientDebugFrameRequestMessage& rMessage)
	{
		rVisitor.Type(keType);
		rVisitor.Field(rMessage.iTick);
		rVisitor.Field(rMessage.coord);
	}
};

struct ClientHelloMessage
{
	static constexpr PacketType keType = PacketType::kClientHello;
	static constexpr int64_t kiFixedSize = kiPacketTypeSize + sizeof(uint32_t) + sizeof(int64_t) + sizeof(common::crc_t);
	static constexpr int64_t kiMinSize = kiFixedSize;
	static constexpr int64_t kiMaxBuildConfigBytes = 64;
	static constexpr int64_t kiMaxSize = kiFixedSize + kiMaxBuildConfigBytes + kiGuidSize;

	uint32_t uiProtocolVersion = 0;
	int64_t iFrameVersion = 0;
	common::crc_t packIntegrityToken = 0;
	std::string_view buildConfig;
	ClientGuid guid {};
	bool bHasGuid = false;

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ClientHelloMessage& rMessage)
	{
		rVisitor.Type(keType);
		rVisitor.Field(rMessage.uiProtocolVersion);
		rVisitor.Field(rMessage.iFrameVersion);
		rVisitor.Field(rMessage.packIntegrityToken);
		rVisitor.NullTerminatedString(rMessage.buildConfig);
		rVisitor.OptionalGuid(rMessage.guid, rMessage.bHasGuid);
	}
};

struct ServerConnectionResponseMessage
{
	static constexpr PacketType keType = PacketType::kServerConnectionResponse;
	static constexpr int64_t kiMinSize = kiPacketTypeSize + sizeof(uint8_t);
	static constexpr int64_t kiAcceptedGuidSize = kiMinSize + kiGuidSize;

	uint8_t uiAccepted = 0;
	ClientGuid guid {};
	bool bHasGuid = false;
	std::string_view rejectionMessage;

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ServerConnectionResponseMessage& rMessage)
	{
		rVisitor.Type(keType);
		rVisitor.Field(rMessage.uiAccepted);
		rVisitor.ConnectionResponseTail(rMessage.uiAccepted, rMessage.guid, rMessage.bHasGuid, rMessage.rejectionMessage);
	}
};

struct ClientSubscribeMessage
{
	static constexpr PacketType keType = PacketType::kClientSubscribe;
	static constexpr int64_t kiFixedSize = kiPacketTypeSize + kiGridCoordSize;

	GridCoord coord {};

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ClientSubscribeMessage& rMessage)
	{
		rVisitor.Type(keType);
		rVisitor.Field(rMessage.coord);
	}
};

struct ClientUnsubscribeMessage
{
	static constexpr PacketType keType = PacketType::kClientUnsubscribe;
	static constexpr int64_t kiFixedSize = kiPacketTypeSize + sizeof(uint8_t) + sizeof(uint16_t);

	uint8_t uiSlotIndex = 0;
	uint16_t uiEpoch = 0;

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ClientUnsubscribeMessage& rMessage)
	{
		rVisitor.Type(keType);
		rVisitor.Field(rMessage.uiSlotIndex);
		rVisitor.Field(rMessage.uiEpoch);
	}
};

struct ClientResyncRequestMessage
{
	static constexpr PacketType keType = PacketType::kClientResyncRequest;
	static constexpr int64_t kiFixedSize = kiPacketTypeSize;

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ClientResyncRequestMessage&)
	{
		rVisitor.Type(keType);
	}
};

struct ServerSubscribeAcceptMessage
{
	static constexpr PacketType keType = PacketType::kServerSubscribeAccept;
	static constexpr int64_t kiFixedSize = kiPacketTypeSize + sizeof(uint8_t) + sizeof(uint16_t) + kiGridCoordSize;

	uint8_t uiSlotIndex = 0;
	uint16_t uiEpoch = 0;
	GridCoord coord {};

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ServerSubscribeAcceptMessage& rMessage)
	{
		rVisitor.Type(keType);
		rVisitor.Field(rMessage.uiSlotIndex);
		rVisitor.Field(rMessage.uiEpoch);
		rVisitor.Field(rMessage.coord);
	}
};

struct ServerUnsubscribeAckMessage
{
	static constexpr PacketType keType = PacketType::kServerUnsubscribeAck;
	static constexpr int64_t kiFixedSize = kiPacketTypeSize + sizeof(uint8_t);

	uint8_t uiSlotIndex = 0;

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ServerUnsubscribeAckMessage& rMessage)
	{
		rVisitor.Type(keType);
		rVisitor.Field(rMessage.uiSlotIndex);
	}
};

struct ServerLoadNotificationMessage
{
	static constexpr PacketType keType = PacketType::kServerLoadNotification;
	static constexpr int64_t kiFixedSize = kiPacketTypeSize;

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ServerLoadNotificationMessage&)
	{
		rVisitor.Type(keType);
	}
};

struct ServerCoordFullStateMessage
{
	static constexpr PacketType keType = PacketType::kServerCoordFullState;
	static constexpr int64_t kiEnvelopeSize = kiPacketTypeSize + sizeof(uint8_t) + sizeof(uint16_t) + sizeof(int64_t) + kiGridCoordSize;
	static constexpr int64_t kiFixedSize = kiEnvelopeSize + sizeof(int32_t) + sizeof(int32_t);

	uint8_t uiSlotIndex = 0;
	uint16_t uiEpoch = 0;
	int64_t iTick = 0;
	GridCoord coord {};
	int32_t iUncompressedSize = 0;
	PacketPayload compressedPayload {};

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ServerCoordFullStateMessage& rMessage)
	{
		rVisitor.Type(keType);
		rVisitor.Field(rMessage.uiSlotIndex);
		rVisitor.Field(rMessage.uiEpoch);
		rVisitor.Field(rMessage.iTick);
		rVisitor.Field(rMessage.coord);
		rVisitor.Field(rMessage.iUncompressedSize);
		rVisitor.Field(rMessage.compressedPayload.iSize);
		rVisitor.Payload(rMessage.compressedPayload);
	}
};

struct ServerCoordStaticDataMessage
{
	static constexpr PacketType keType = PacketType::kServerCoordStaticData;
	static constexpr int64_t kiEnvelopeSize = kiPacketTypeSize + sizeof(uint8_t) + sizeof(uint16_t) + kiGridCoordSize;
	static constexpr int64_t kiFixedSize = kiEnvelopeSize + sizeof(int32_t);

	uint8_t uiSlotIndex = 0;
	uint16_t uiEpoch = 0;
	GridCoord coord {};
	PacketPayload staticData {};

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ServerCoordStaticDataMessage& rMessage)
	{
		rVisitor.Type(keType);
		rVisitor.Field(rMessage.uiSlotIndex);
		rVisitor.Field(rMessage.uiEpoch);
		rVisitor.Field(rMessage.coord);
		rVisitor.Field(rMessage.staticData.iSize);
		rVisitor.Payload(rMessage.staticData);
	}
};

struct CoordUpdateFields
{
	static constexpr int64_t kiEnvelopeSize = kiPacketTypeSize + sizeof(uint8_t) + sizeof(uint16_t) + sizeof(int64_t) + sizeof(int64_t) + sizeof(uint64_t);
	static constexpr int64_t kiFixedSize = kiEnvelopeSize + sizeof(int32_t);

	uint8_t uiSlotIndex = 0;
	uint16_t uiEpoch = 0;
	int64_t iTick = 0;
	int64_t iEchoedTimestampNs = 0;
	uint64_t uiSharedCrc = 0;
	PacketPayload compressedPayload {};

	template <typename TVisitor>
	static void VisitFields(TVisitor& rVisitor, CoordUpdateFields& rMessage)
	{
		rVisitor.Field(rMessage.uiSlotIndex);
		rVisitor.Field(rMessage.uiEpoch);
		rVisitor.Field(rMessage.iTick);
		rVisitor.Field(rMessage.iEchoedTimestampNs);
		rVisitor.Field(rMessage.uiSharedCrc);
		rVisitor.Field(rMessage.compressedPayload.iSize);
		rVisitor.Payload(rMessage.compressedPayload);
	}
};

struct ServerCoordUpdateMessage : CoordUpdateFields
{
	static constexpr PacketType keType = PacketType::kServerCoordUpdate;
	static constexpr int64_t kiEnvelopeSize = CoordUpdateFields::kiEnvelopeSize;
	static constexpr int64_t kiFixedSize = CoordUpdateFields::kiFixedSize;

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ServerCoordUpdateMessage& rMessage)
	{
		rVisitor.Type(keType);
		VisitFields(rVisitor, rMessage);
	}
};

struct ServerCoordResendMessage : CoordUpdateFields
{
	static constexpr PacketType keType = PacketType::kServerCoordResend;
	static constexpr int64_t kiEnvelopeSize = CoordUpdateFields::kiEnvelopeSize;
	static constexpr int64_t kiFixedSize = CoordUpdateFields::kiFixedSize;

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ServerCoordResendMessage& rMessage)
	{
		rVisitor.Type(keType);
		VisitFields(rVisitor, rMessage);
	}
};

struct ServerDebugFrameMessage
{
	static constexpr PacketType keType = PacketType::kServerDebugFrame;
	static constexpr int64_t kiEnvelopeSize = kiPacketTypeSize + sizeof(int64_t) + kiGridCoordSize;
	static constexpr int64_t kiFixedSize = kiEnvelopeSize + sizeof(int32_t) + sizeof(int32_t);

	int64_t iTick = 0;
	GridCoord coord {};
	int32_t iUncompressedSize = 0;
	PacketPayload compressedPayload {};

	template <typename TVisitor>
	static void Visit(TVisitor& rVisitor, ServerDebugFrameMessage& rMessage)
	{
		rVisitor.Type(keType);
		rVisitor.Field(rMessage.iTick);
		rVisitor.Field(rMessage.coord);
		rVisitor.Field(rMessage.iUncompressedSize);
		rVisitor.Field(rMessage.compressedPayload.iSize);
		rVisitor.Payload(rMessage.compressedPayload);
	}
};

inline int64_t GetCoordUpdateTickOrZero(std::span<const uint8_t> packetData)
{
	ServerCoordUpdateMessage update {};
	CoordUpdateTickReader updateReader {packetData};
	ServerCoordUpdateMessage::Visit(updateReader, update);
	if (updateReader.HasTick())
	{
		return updateReader.GetTick();
	}

	ServerCoordResendMessage resend {};
	CoordUpdateTickReader resendReader {packetData};
	ServerCoordResendMessage::Visit(resendReader, resend);
	return resendReader.HasTick() ? resendReader.GetTick() : 0;
}

} // namespace engine::NetworkMessages

#include "Pch.h"

#include "Network/NetworkSerialization.h"

#include "Network/NetworkCursor.h"

#include "Frame/StatusChange.h"

namespace engine
{

// Per-type serialize helpers
static void SerializeBlasterTransfer(uint8_t*& pCursor, const game::TransferData& rData)
{
	WriteVec4(pCursor, rData.vecPosition);
	WriteVec4(pCursor, rData.vecVelocity);
	WriteUint8(pCursor, rData.uiTypeIndex);
	WriteUint32(pCursor, rData.alignment.uiValue);
	WriteFloat(pCursor, rData.fWindTrailIntensity);
	WriteFloat(pCursor, rData.fWindTrailWidth);
	WriteFloat(pCursor, rData.fWindTrailLengthMultiplier);
}

static void SerializeSpaceshipTransfer(uint8_t*& pCursor, const game::TransferData& rData)
{
	WriteVec4(pCursor, rData.vecPosition);
	WriteVec4(pCursor, rData.vecDirection);
	WriteVec4(pCursor, rData.vecVelocity);
	WriteUint32(pCursor, rData.alignment.uiValue);
	WriteFloat(pCursor, rData.fHealth);
	WriteFloat(pCursor, rData.fNextBlasterSpawnTime);
	WriteFloat(pCursor, rData.fDeltaRotation);
}

static void SerializeMissileTransfer(uint8_t*& pCursor, const game::TransferData& rData)
{
	WriteVec4(pCursor, rData.vecPosition);
	WriteVec4(pCursor, rData.vecDirection);
	WriteVec4(pCursor, rData.vecVelocity);
	WriteUint32(pCursor, rData.alignment.uiValue);
	WriteFloat(pCursor, rData.fAcceleration);
	WriteFloat(pCursor, rData.fDeltaRotationDelay);
	WriteFloat(pCursor, rData.fTime);
	WriteFloat(pCursor, rData.fNextJitter);
	WriteFloat(pCursor, rData.fDeltaRotation);
	WriteFloat(pCursor, rData.fDeltaRotationMax);
	WriteFloat(pCursor, rData.fPitch);
#if defined(BT_CLIENT)
	int64_t iSmokeTrailId = rData.smokeTrailId.ToUuid().Value();
#else
	int64_t iSmokeTrailId = 0;
#endif
	WriteInt64(pCursor, iSmokeTrailId);
}

static void SerializePlayerTransfer(uint8_t*& pCursor, const game::TransferData& rData)
{
	WriteVec4(pCursor, rData.vecPosition);
	WriteVec4(pCursor, rData.vecDirection);
	WriteVec4(pCursor, rData.vecVelocity);
	WriteUint32(pCursor, rData.alignment.uiValue);
	WriteFloat(pCursor, rData.fHealth);
	WriteFloat(pCursor, rData.fShield);
	WriteFloat(pCursor, rData.fNextBlasterFireTime);
	WriteFloat(pCursor, rData.fNextSecondarySpawnTime);
	WriteFloat(pCursor, rData.fShieldCooldown);
	WriteFloat(pCursor, rData.fShieldDownSoundCooldown);
	WriteFloat(pCursor, rData.fAnimationTime);
	WriteFloat(pCursor, rData.fShieldRotation);
	WriteFloat(pCursor, rData.fShieldShrink);
	WriteUint16(pCursor, rData.uiPlayerFlags);
	WriteFloat(pCursor, rData.fNavigationDelay);
	WriteInt64(pCursor, rData.globalPlayerId.iValue);
	WriteGridCoord(pCursor, rData.fleetWantedCoord);
	WriteUint8(pCursor, rData.uiPendingFleetWantedCoordTicks);
	WriteUint8(pCursor, rData.uiPendingWeaponModeTicks);
}

// Per-type deserialize helpers
static void DeserializeBlasterTransfer(const uint8_t*& pCursor, game::TransferData& rData)
{
	rData.vecPosition = ReadVec4(pCursor);
	rData.vecVelocity = ReadVec4(pCursor);
	rData.uiTypeIndex = ReadUint8(pCursor);
	rData.alignment = alignment_t(ReadUint32(pCursor));
	rData.fWindTrailIntensity = ReadFloat(pCursor);
	rData.fWindTrailWidth = ReadFloat(pCursor);
	rData.fWindTrailLengthMultiplier = ReadFloat(pCursor);
}

static void DeserializeSpaceshipTransfer(const uint8_t*& pCursor, game::TransferData& rData)
{
	rData.vecPosition = ReadVec4(pCursor);
	rData.vecDirection = ReadVec4(pCursor);
	rData.vecVelocity = ReadVec4(pCursor);
	rData.alignment = alignment_t(ReadUint32(pCursor));
	rData.fHealth = ReadFloat(pCursor);
	rData.fNextBlasterSpawnTime = ReadFloat(pCursor);
	rData.fDeltaRotation = ReadFloat(pCursor);
}

static void DeserializeMissileTransfer(const uint8_t*& pCursor, game::TransferData& rData)
{
	rData.vecPosition = ReadVec4(pCursor);
	rData.vecDirection = ReadVec4(pCursor);
	rData.vecVelocity = ReadVec4(pCursor);
	rData.alignment = alignment_t(ReadUint32(pCursor));
	rData.fAcceleration = ReadFloat(pCursor);
	rData.fDeltaRotationDelay = ReadFloat(pCursor);
	rData.fTime = ReadFloat(pCursor);
	rData.fNextJitter = ReadFloat(pCursor);
	rData.fDeltaRotation = ReadFloat(pCursor);
	rData.fDeltaRotationMax = ReadFloat(pCursor);
	rData.fPitch = ReadFloat(pCursor);
	[[maybe_unused]] int64_t iSmokeTrailId = ReadInt64(pCursor);
#if defined(BT_CLIENT)
	rData.smokeTrailId = smoke_trails_t(engine::uuid_t(iSmokeTrailId));
#endif
}

static void DeserializePlayerTransfer(const uint8_t*& pCursor, game::TransferData& rData)
{
	rData.vecPosition = ReadVec4(pCursor);
	rData.vecDirection = ReadVec4(pCursor);
	rData.vecVelocity = ReadVec4(pCursor);
	rData.alignment = alignment_t(ReadUint32(pCursor));
	rData.fHealth = ReadFloat(pCursor);
	rData.fShield = ReadFloat(pCursor);
	rData.fNextBlasterFireTime = ReadFloat(pCursor);
	rData.fNextSecondarySpawnTime = ReadFloat(pCursor);
	rData.fShieldCooldown = ReadFloat(pCursor);
	rData.fShieldDownSoundCooldown = ReadFloat(pCursor);
	rData.fAnimationTime = ReadFloat(pCursor);
	rData.fShieldRotation = ReadFloat(pCursor);
	rData.fShieldShrink = ReadFloat(pCursor);
	rData.uiPlayerFlags = ReadUint16(pCursor);
	rData.fNavigationDelay = ReadFloat(pCursor);
	rData.globalPlayerId.iValue = ReadInt64(pCursor);
	rData.fleetWantedCoord = ReadGridCoord(pCursor);
	rData.uiPendingFleetWantedCoordTicks = ReadUint8(pCursor);
	rData.uiPendingWeaponModeTicks = ReadUint8(pCursor);
}

// Serialized wire size of one item of each StatusChangeType, excluding the per-group [type:1][count:2] header.
// Receive-side mirror of the per-case write widths in SerializeGroup (and the Serialize* helpers) below —
// DeserializeStatusChangeBatch bounds-checks each item against this before reading, then rejects the whole batch on
// any shortfall. Any StatusChangeType payload change must update this alongside the two switches, DefaultDataForType,
// and kiMaxStatusChangeBytesPerItem.
static int64_t StatusChangeItemWireSize(game::StatusChangeType eType)
{
	static constexpr int64_t kiU8 = sizeof(uint8_t);
	static constexpr int64_t kiU16 = sizeof(uint16_t);
	static constexpr int64_t kiU32 = sizeof(uint32_t);
	static constexpr int64_t kiI64 = sizeof(int64_t);
	static constexpr int64_t kiF32 = sizeof(float);
	static constexpr int64_t kiVec4 = sizeof(XMFLOAT4A);
	static constexpr int64_t kiCoord = 2 * kiU32; // GridCoord = two int32

	switch (eType)
	{
		case game::StatusChangeType::kSpawnPlayer:       return kiI64 + kiU8 + kiCoord + kiU8;
		case game::StatusChangeType::kRespawnPlayer:     return 0;
		case game::StatusChangeType::kTransferBlaster:   return 2 * kiVec4 + kiU8 + kiU32 + 3 * kiF32;
		case game::StatusChangeType::kTransferSpaceship: return 3 * kiVec4 + kiU32 + 3 * kiF32;
		case game::StatusChangeType::kTransferMissile:   return 3 * kiVec4 + kiU32 + 7 * kiF32 + kiI64;
		case game::StatusChangeType::kTransferPlayer:    return 3 * kiVec4 + kiU32 + 10 * kiF32 + kiU16 + kiI64 + kiCoord + 2 * kiU8;
		case game::StatusChangeType::kDestroyPlayer:     return kiI64;
		case game::StatusChangeType::kUpdatePlayer:      return kiI64 + kiU8 + kiF32 + kiU8;
		case game::StatusChangeType::kUpdateFleet:       return kiI64 + kiU8 + kiCoord + kiU8;
	}

	return 0;
}

// Serialize a group of StatusChanges that share the same type
static void SerializeGroup(uint8_t*& pCursor, game::StatusChangeType eType, const game::StatusChange* pChanges, const int64_t* pIndices, int64_t iGroupCount)
{
	if (iGroupCount == 0)
	{
		return;
	}

	WriteUint8(pCursor, static_cast<uint8_t>(eType));
	WriteUint16(pCursor, static_cast<uint16_t>(iGroupCount));

	for (int64_t i = 0; i < iGroupCount; ++i)
	{
		const game::StatusChangeData& rData = pChanges[pIndices[i]].data;
		const uint8_t* pItemStart = pCursor;

		switch (eType)
		{
			case game::StatusChangeType::kSpawnPlayer:
			{
				const game::SpawnPlayerData& rSpawn = std::get<game::SpawnPlayerData>(rData);
				WriteInt64(pCursor, rSpawn.iGlobalId);
				WriteUint8(pCursor, rSpawn.bIsFlagship ? 1 : 0);
				WriteGridCoord(pCursor, rSpawn.fleetWantedCoord);
				WriteUint8(pCursor, rSpawn.uiPendingFleetWantedCoordTicks);
				break;
			}
			case game::StatusChangeType::kRespawnPlayer:
				break;
			case game::StatusChangeType::kTransferBlaster:
				SerializeBlasterTransfer(pCursor, std::get<game::TransferData>(rData));
				break;
			case game::StatusChangeType::kTransferSpaceship:
				SerializeSpaceshipTransfer(pCursor, std::get<game::TransferData>(rData));
				break;
			case game::StatusChangeType::kTransferMissile:
				SerializeMissileTransfer(pCursor, std::get<game::TransferData>(rData));
				break;
			case game::StatusChangeType::kTransferPlayer:
				SerializePlayerTransfer(pCursor, std::get<game::TransferData>(rData));
				break;
			case game::StatusChangeType::kDestroyPlayer:
				WriteInt64(pCursor, std::get<game::DestroyPlayerData>(rData).iPlayerUuid);
				break;
			case game::StatusChangeType::kUpdatePlayer:
			{
				const game::UpdatePlayerData& rUpdate = std::get<game::UpdatePlayerData>(rData);
				WriteInt64(pCursor, rUpdate.iPlayerUuid);
				WriteUint8(pCursor, rUpdate.bUseMissiles ? 1 : 0);
				WriteFloat(pCursor, rUpdate.fNavigationDelay);
				WriteUint8(pCursor, rUpdate.uiPendingWeaponModeTicks);
				break;
			}
			case game::StatusChangeType::kUpdateFleet:
			{
				const game::UpdateFleetData& rUpdate = std::get<game::UpdateFleetData>(rData);
				WriteInt64(pCursor, rUpdate.iPlayerUuid);
				WriteUint8(pCursor, rUpdate.bIsFlagship ? 1 : 0);
				WriteGridCoord(pCursor, rUpdate.fleetWantedCoord);
				WriteUint8(pCursor, rUpdate.uiPendingFleetWantedCoordTicks);
				break;
			}
		}

		ASSERT(pCursor - pItemStart <= kiMaxStatusChangeBytesPerItem);
	}
}

constexpr int64_t kiTypeCount = static_cast<int64_t>(game::StatusChangeType::kCount);

static void GroupIndicesByType(const game::StatusChange* pChanges, int64_t iCount, int64_t piOffsets[kiTypeCount], int64_t piCounts[kiTypeCount], int64_t* pSortedIndices)
{
	for (int64_t i = 0; i < iCount; ++i)
	{
		++piCounts[static_cast<int64_t>(pChanges[i].eType)];
	}

	for (int64_t t = 1; t < kiTypeCount; ++t)
	{
		piOffsets[t] = piOffsets[t - 1] + piCounts[t - 1];
	}

	int64_t piWriteOffsets[kiTypeCount] = {};
	std::memcpy(piWriteOffsets, piOffsets, sizeof(int64_t) * kiTypeCount);
	for (int64_t i = 0; i < iCount; ++i)
	{
		int64_t iType = static_cast<int64_t>(pChanges[i].eType);
		pSortedIndices[piWriteOffsets[iType]++] = i;
	}
}

int64_t SerializeStatusChangeBatch(const game::StatusChange* pChanges, int64_t iCount, void* pDest)
{
	if (iCount == 0)
	{
		return 0;
	}

	uint8_t* pCursor = static_cast<uint8_t*>(pDest);

	// Group indices by type in a workbuffer reservation (GroupIndicesByType writes every slot, so no zero-fill)
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferAllocation<int64_t*> sortedAllocation = rWorkbuffer.PushBuffer<int64_t*>(iCount * static_cast<int64_t>(sizeof(int64_t)));
	int64_t* pSorted = sortedAllocation;

	int64_t piGroupCounts[kiTypeCount] = {};
	int64_t piOffsets[kiTypeCount] = {};
	GroupIndicesByType(pChanges, iCount, piOffsets, piGroupCounts, pSorted);

	// Serialize each type group
	for (int64_t t = 0; t < kiTypeCount; ++t)
	{
		SerializeGroup(pCursor, static_cast<game::StatusChangeType>(t), pChanges, pSorted + piOffsets[t], piGroupCounts[t]);
	}

	return pCursor - static_cast<uint8_t*>(pDest);
}

int64_t DeserializeStatusChangeBatch(const void* pSource, int64_t iSourceSize, game::StatusChange* pDest, int64_t iMaxCount)
{
	if (iSourceSize == 0)
	{
		return 0;
	}

	// Trust boundary (network input): drive every read through a bounded cursor and reject the whole batch on any
	// shortfall or out-of-range type byte, rather than over-reading pEnd or emitting a defaulted item. No partial
	// application — the client resyncs via CRC. StatusChangeItemWireSize above is the per-type read-width mirror.
	BoundedCursor cursor {static_cast<const uint8_t*>(pSource), static_cast<const uint8_t*>(pSource) + iSourceSize};
	int64_t iOutputCount = 0;

	// [type:1][count:2] group header, then uiGroupCount items of StatusChangeItemWireSize(eType) bytes each.
	while (cursor.Has(static_cast<int64_t>(sizeof(uint8_t) + sizeof(uint16_t))) && iOutputCount < iMaxCount)
	{
		uint8_t uiType = ReadUint8(cursor.pCursor);
		uint16_t uiGroupCount = ReadUint16(cursor.pCursor);

		if (uiType >= kiTypeCount)
		{
			LOG(kNetwork, kWarning, "DeserializeStatusChangeBatch: out-of-range type byte {} (deserialized {})", static_cast<int>(uiType), iOutputCount);
			return 0;
		}

		game::StatusChangeType eType = static_cast<game::StatusChangeType>(uiType);
		int64_t iItemWireSize = StatusChangeItemWireSize(eType);

		for (uint16_t i = 0; i < uiGroupCount && iOutputCount < iMaxCount; ++i)
		{
			if (!cursor.Has(iItemWireSize))
			{
				LOG(kNetwork, kWarning, "DeserializeStatusChangeBatch: truncated mid-group (type {}, deserialized {})", static_cast<int>(uiType), iOutputCount);
				return 0;
			}

			game::StatusChange& rChange = pDest[iOutputCount++];
			rChange = {};
			rChange.eType = eType;

			rChange.data = game::DefaultDataForType(eType);

			switch (eType)
			{
				case game::StatusChangeType::kSpawnPlayer:
				{
					game::SpawnPlayerData& rSpawn = std::get<game::SpawnPlayerData>(rChange.data);
					rSpawn.iGlobalId = ReadInt64(cursor.pCursor);
					rSpawn.bIsFlagship = ReadUint8(cursor.pCursor) != 0;
					rSpawn.fleetWantedCoord = ReadGridCoord(cursor.pCursor);
					rSpawn.uiPendingFleetWantedCoordTicks = ReadUint8(cursor.pCursor);
					break;
				}
				case game::StatusChangeType::kRespawnPlayer:
					break;
				case game::StatusChangeType::kTransferBlaster:
					DeserializeBlasterTransfer(cursor.pCursor, std::get<game::TransferData>(rChange.data));
					break;
				case game::StatusChangeType::kTransferSpaceship:
					DeserializeSpaceshipTransfer(cursor.pCursor, std::get<game::TransferData>(rChange.data));
					break;
				case game::StatusChangeType::kTransferMissile:
					DeserializeMissileTransfer(cursor.pCursor, std::get<game::TransferData>(rChange.data));
					break;
				case game::StatusChangeType::kTransferPlayer:
					DeserializePlayerTransfer(cursor.pCursor, std::get<game::TransferData>(rChange.data));
					break;
				case game::StatusChangeType::kDestroyPlayer:
					std::get<game::DestroyPlayerData>(rChange.data).iPlayerUuid = ReadInt64(cursor.pCursor);
					break;
				case game::StatusChangeType::kUpdatePlayer:
				{
					game::UpdatePlayerData& rUpdate = std::get<game::UpdatePlayerData>(rChange.data);
					rUpdate.iPlayerUuid = ReadInt64(cursor.pCursor);
					rUpdate.bUseMissiles = ReadUint8(cursor.pCursor) != 0;
					rUpdate.fNavigationDelay = ReadFloat(cursor.pCursor);
					rUpdate.uiPendingWeaponModeTicks = ReadUint8(cursor.pCursor);
					break;
				}
				case game::StatusChangeType::kUpdateFleet:
				{
					game::UpdateFleetData& rUpdate = std::get<game::UpdateFleetData>(rChange.data);
					rUpdate.iPlayerUuid = ReadInt64(cursor.pCursor);
					rUpdate.bIsFlagship = ReadUint8(cursor.pCursor) != 0;
					rUpdate.fleetWantedCoord = ReadGridCoord(cursor.pCursor);
					rUpdate.uiPendingFleetWantedCoordTicks = ReadUint8(cursor.pCursor);
					break;
				}
			}
		}
	}

	return iOutputCount;
}

int64_t CompressStatusChangeBatch(const game::StatusChange* pChanges, int64_t iCount, void* pDest, int64_t iDestCapacity)
{
	if (iCount == 0)
	{
		return 0;
	}

	// Serialize into a workbuffer reservation (SerializeStatusChangeBatch writes only iSerializedSize bytes and only
	// those are compressed, so no zero-fill), then LZ4 compress into pDest
	constexpr int64_t kiMaxGroupHeaders = kiTypeCount * 3;
	int64_t iMaxSerializedSize = kiMaxGroupHeaders + iCount * kiMaxStatusChangeBytesPerItem;

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferAllocation<uint8_t*> serializedAllocation = rWorkbuffer.PushBuffer<uint8_t*>(iMaxSerializedSize);
	uint8_t* pSerialized = serializedAllocation;

	int64_t iSerializedSize = SerializeStatusChangeBatch(pChanges, iCount, pSerialized);

	// Write 4-byte uncompressed size prefix, then LZ4 compressed data
	uint8_t* pOutput = static_cast<uint8_t*>(pDest);
	int32_t iUncompressedSize = static_cast<int32_t>(iSerializedSize);
	std::memcpy(pOutput, &iUncompressedSize, sizeof(int32_t));

	int iCompressedSize = LZ4_compress_default(reinterpret_cast<const char*>(pSerialized), reinterpret_cast<char*>(pOutput + sizeof(int32_t)), static_cast<int>(iSerializedSize), static_cast<int>(iDestCapacity - sizeof(int32_t)));
	if (iCompressedSize <= 0)
	{
		// Belt (the caller sizes pDest to fit any valid capped batch): a 0 return means the batch did not fit. Drop it
		// rather than ship the 4-byte prefix alone, which the client would decode as zero changes and silently desync.
		LOG(kNetwork, kError, "CompressStatusChangeBatch: LZ4 compression failed (items {}, serialized {}, dest capacity {})", iCount, iSerializedSize, iDestCapacity);
		return 0;
	}

	return static_cast<int64_t>(sizeof(int32_t)) + iCompressedSize;
}

int64_t DecompressStatusChangeBatch(const void* pSource, int64_t iSourceSize, game::StatusChange* pDest, int64_t iMaxCount)
{
	if (iSourceSize <= static_cast<int64_t>(sizeof(int32_t)))
	{
		return 0;
	}

	// Read uncompressed size prefix
	const uint8_t* pInput = static_cast<const uint8_t*>(pSource);
	int32_t iUncompressedSize = 0;
	std::memcpy(&iUncompressedSize, pInput, sizeof(int32_t));

	// Trust boundary (network input): clamp the wire-controlled size against the true maximum serialized batch size
	// before it drives the decompress-buffer reservation, rejecting a hostile prefix that would otherwise allocate up
	// to ~2 GB. A valid batch's uncompressed size is at or below this bound by construction (the send side sizes its
	// compress scratch from the same constant).
	if (iUncompressedSize <= 0 || iUncompressedSize > kiMaxSerializedStatusChangeBatchBytes)
	{
		return 0;
	}

	// Decompress into a workbuffer reservation (LZ4 writes iResult bytes and only those are deserialized, so no
	// zero-fill; item (b)'s bounded reads never run past iResult, so the prior chunk-rounded slack is unneeded)
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferAllocation<uint8_t*> decompressedAllocation = rWorkbuffer.PushBuffer<uint8_t*>(iUncompressedSize);
	uint8_t* pDecompressed = decompressedAllocation;

	int iResult = LZ4_decompress_safe(reinterpret_cast<const char*>(pInput + sizeof(int32_t)), reinterpret_cast<char*>(pDecompressed), static_cast<int>(iSourceSize - sizeof(int32_t)), iUncompressedSize);

	if (iResult <= 0)
	{
		LOG(kNetwork, kWarning, "DecompressStatusChangeBatch: LZ4 decompression failed (error {})", iResult);
		return 0;
	}

	int64_t iCount = DeserializeStatusChangeBatch(pDecompressed, iResult, pDest, iMaxCount);

	return iCount;
}

} // namespace engine

#include "Pch.h"

#include "Network/NetworkSerialization.h"

#include "Input/Input.h"

namespace engine
{

// Cursor write helpers
static void WriteBytes(uint8_t*& pCursor, const void* pData, int64_t iSize)
{
	std::memcpy(pCursor, pData, iSize);
	pCursor += iSize;
}

static void WriteFloat(uint8_t*& pCursor, float f)
{
	WriteBytes(pCursor, &f, sizeof(float));
}

static void WriteUint8(uint8_t*& pCursor, uint8_t u)
{
	*pCursor++ = u;
}

static void WriteUint16(uint8_t*& pCursor, uint16_t u)
{
	WriteBytes(pCursor, &u, sizeof(uint16_t));
}

static void WriteUint32(uint8_t*& pCursor, uint32_t u)
{
	WriteBytes(pCursor, &u, sizeof(uint32_t));
}

static void WriteVec(uint8_t*& pCursor, XMVECTOR vec)
{
	XMFLOAT4A f4;
	XMStoreFloat4A(&f4, vec);
	WriteBytes(pCursor, &f4, sizeof(XMFLOAT4A));
}

// Cursor read helpers
static void ReadBytes(const uint8_t*& pCursor, void* pDest, int64_t iSize)
{
	std::memcpy(pDest, pCursor, iSize);
	pCursor += iSize;
}

static float ReadFloat(const uint8_t*& pCursor)
{
	float f;
	ReadBytes(pCursor, &f, sizeof(float));
	return f;
}

static uint8_t ReadUint8(const uint8_t*& pCursor)
{
	return *pCursor++;
}

static uint16_t ReadUint16(const uint8_t*& pCursor)
{
	uint16_t u;
	ReadBytes(pCursor, &u, sizeof(uint16_t));
	return u;
}

static uint32_t ReadUint32(const uint8_t*& pCursor)
{
	uint32_t u;
	ReadBytes(pCursor, &u, sizeof(uint32_t));
	return u;
}

static XMVECTOR ReadVec(const uint8_t*& pCursor)
{
	XMFLOAT4A f4;
	ReadBytes(pCursor, &f4, sizeof(XMFLOAT4A));
	return XMLoadFloat4A(&f4);
}

// Per-type serialize helpers
static void SerializeBlasterTransfer(uint8_t*& pCursor, const game::TransferData& rData)
{
	WriteVec(pCursor, rData.vecPosition);
	WriteVec(pCursor, rData.vecVelocity);
	WriteUint8(pCursor, rData.uiTypeIndex);
	WriteUint32(pCursor, rData.alignment.uiValue);
	WriteFloat(pCursor, rData.fWindTrailIntensity);
	WriteFloat(pCursor, rData.fWindTrailWidth);
	WriteFloat(pCursor, rData.fWindTrailLengthMultiplier);
}

static void SerializeSpaceshipTransfer(uint8_t*& pCursor, const game::TransferData& rData)
{
	WriteVec(pCursor, rData.vecPosition);
	WriteVec(pCursor, rData.vecDirection);
	WriteVec(pCursor, rData.vecVelocity);
	WriteUint32(pCursor, rData.alignment.uiValue);
	WriteFloat(pCursor, rData.fHealth);
	WriteFloat(pCursor, rData.fNextBlasterSpawnTime);
}

static void SerializeMissileTransfer(uint8_t*& pCursor, const game::TransferData& rData)
{
	WriteVec(pCursor, rData.vecPosition);
	WriteVec(pCursor, rData.vecDirection);
	WriteVec(pCursor, rData.vecVelocity);
	WriteUint32(pCursor, rData.alignment.uiValue);
	WriteFloat(pCursor, rData.fAcceleration);
	WriteFloat(pCursor, rData.fDeltaRotationDelay);
	WriteFloat(pCursor, rData.fTime);
	WriteFloat(pCursor, rData.fExhaustDelay);
	WriteFloat(pCursor, rData.fNextJitter);
#ifdef BT_CLIENT
	int64_t iSmokeTrailId = rData.smokeTrailId.ToUuid().Value();
#else
	int64_t iSmokeTrailId = 0;
#endif
	WriteBytes(pCursor, &iSmokeTrailId, sizeof(int64_t));
}

static void SerializePlayerTransfer(uint8_t*& pCursor, const game::TransferData& rData)
{
	WriteVec(pCursor, rData.vecPosition);
	WriteVec(pCursor, rData.vecDirection);
	WriteVec(pCursor, rData.vecVelocity);
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
	WriteUint8(pCursor, rData.uiPlayerFlags);
}

// Per-type deserialize helpers
static void DeserializeBlasterTransfer(const uint8_t*& pCursor, game::TransferData& rData)
{
	rData.vecPosition = ReadVec(pCursor);
	rData.vecVelocity = ReadVec(pCursor);
	rData.uiTypeIndex = ReadUint8(pCursor);
	rData.alignment = alignment_t(ReadUint32(pCursor));
	rData.fWindTrailIntensity = ReadFloat(pCursor);
	rData.fWindTrailWidth = ReadFloat(pCursor);
	rData.fWindTrailLengthMultiplier = ReadFloat(pCursor);
}

static void DeserializeSpaceshipTransfer(const uint8_t*& pCursor, game::TransferData& rData)
{
	rData.vecPosition = ReadVec(pCursor);
	rData.vecDirection = ReadVec(pCursor);
	rData.vecVelocity = ReadVec(pCursor);
	rData.alignment = alignment_t(ReadUint32(pCursor));
	rData.fHealth = ReadFloat(pCursor);
	rData.fNextBlasterSpawnTime = ReadFloat(pCursor);
}

static void DeserializeMissileTransfer(const uint8_t*& pCursor, game::TransferData& rData)
{
	rData.vecPosition = ReadVec(pCursor);
	rData.vecDirection = ReadVec(pCursor);
	rData.vecVelocity = ReadVec(pCursor);
	rData.alignment = alignment_t(ReadUint32(pCursor));
	rData.fAcceleration = ReadFloat(pCursor);
	rData.fDeltaRotationDelay = ReadFloat(pCursor);
	rData.fTime = ReadFloat(pCursor);
	rData.fExhaustDelay = ReadFloat(pCursor);
	rData.fNextJitter = ReadFloat(pCursor);
	int64_t iSmokeTrailId;
	ReadBytes(pCursor, &iSmokeTrailId, sizeof(int64_t));
#ifdef BT_CLIENT
	rData.smokeTrailId = smoke_trails_t(engine::uuid_t(iSmokeTrailId));
#endif
}

static void DeserializePlayerTransfer(const uint8_t*& pCursor, game::TransferData& rData)
{
	rData.vecPosition = ReadVec(pCursor);
	rData.vecDirection = ReadVec(pCursor);
	rData.vecVelocity = ReadVec(pCursor);
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
	rData.uiPlayerFlags = ReadUint8(pCursor);
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
		WriteUint16(pCursor, pChanges[pIndices[i]].uiSequence);
		const game::TransferData& rData = pChanges[pIndices[i]].data;

		switch (eType)
		{
		case game::StatusChangeType::kSpawnPlayer:
		case game::StatusChangeType::kRespawnPlayer:
			break;
		case game::StatusChangeType::kTransferBlaster:
			SerializeBlasterTransfer(pCursor, rData);
			break;
		case game::StatusChangeType::kTransferSpaceship:
			SerializeSpaceshipTransfer(pCursor, rData);
			break;
		case game::StatusChangeType::kTransferMissile:
			SerializeMissileTransfer(pCursor, rData);
			break;
		case game::StatusChangeType::kTransferPlayer:
			SerializePlayerTransfer(pCursor, rData);
			break;
		}
	}
}

int64_t SerializeStatusChangeBatch(const game::StatusChange* pChanges, int64_t iCount, void* pDest, [[maybe_unused]] int64_t iDestCapacity)
{
	if (iCount == 0)
	{
		return 0;
	}

	uint8_t* pCursor = static_cast<uint8_t*>(pDest);

	// Group indices by type using workbuffer
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// Count per type
	constexpr int64_t kiTypeCount = 6;
	int64_t piGroupCounts[kiTypeCount] = {};
	for (int64_t i = 0; i < iCount; ++i)
	{
		++piGroupCounts[static_cast<int64_t>(pChanges[i].eType)];
	}

	// Build offset table
	int64_t piOffsets[kiTypeCount] = {};
	for (int64_t t = 1; t < kiTypeCount; ++t)
	{
		piOffsets[t] = piOffsets[t - 1] + piGroupCounts[t - 1];
	}

	// Scatter indices by type into workbuffer
	for (int64_t i = 0; i < iCount; ++i)
	{
		rWorkbuffer.PushBack(int64_t(0));
	}
	std::span<const int64_t> sortedSpan = rWorkbuffer.Span<int64_t>();
	int64_t* pSorted = const_cast<int64_t*>(sortedSpan.data());

	int64_t piWriteOffsets[kiTypeCount] = {};
	std::memcpy(piWriteOffsets, piOffsets, sizeof(piOffsets));
	for (int64_t i = 0; i < iCount; ++i)
	{
		int64_t iType = static_cast<int64_t>(pChanges[i].eType);
		pSorted[piWriteOffsets[iType]++] = i;
	}

	// Serialize each type group
	for (int64_t t = 0; t < kiTypeCount; ++t)
	{
		SerializeGroup(pCursor, static_cast<game::StatusChangeType>(t), pChanges, pSorted + piOffsets[t], piGroupCounts[t]);
	}

	rWorkbuffer.Pop();

	return pCursor - static_cast<uint8_t*>(pDest);
}

int64_t DeserializeStatusChangeBatch(const void* pSource, int64_t iSourceSize, game::StatusChange* pDest, int64_t iMaxCount)
{
	if (iSourceSize == 0)
	{
		return 0;
	}

	const uint8_t* pCursor = static_cast<const uint8_t*>(pSource);
	const uint8_t* pEnd = pCursor + iSourceSize;
	int64_t iOutputCount = 0;

	while (pCursor < pEnd && iOutputCount < iMaxCount)
	{
		game::StatusChangeType eType = static_cast<game::StatusChangeType>(ReadUint8(pCursor));
		uint16_t uiGroupCount = ReadUint16(pCursor);

		for (uint16_t i = 0; i < uiGroupCount && iOutputCount < iMaxCount && pCursor < pEnd; ++i)
		{
			game::StatusChange& rChange = pDest[iOutputCount++];
			rChange = {};
			rChange.eType = eType;
			rChange.uiSequence = ReadUint16(pCursor);

			switch (eType)
			{
			case game::StatusChangeType::kSpawnPlayer:
			case game::StatusChangeType::kRespawnPlayer:
				break;
			case game::StatusChangeType::kTransferBlaster:
				DeserializeBlasterTransfer(pCursor, rChange.data);
				break;
			case game::StatusChangeType::kTransferSpaceship:
				DeserializeSpaceshipTransfer(pCursor, rChange.data);
				break;
			case game::StatusChangeType::kTransferMissile:
				DeserializeMissileTransfer(pCursor, rChange.data);
				break;
			case game::StatusChangeType::kTransferPlayer:
				DeserializePlayerTransfer(pCursor, rChange.data);
				break;
			}
		}

		if (pCursor > pEnd)
		{
			common::Log("DeserializeStatusChangeBatch: Data truncated mid-group (type={}, deserialized={})", static_cast<int>(eType), iOutputCount);
			break;
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

	// Serialize into workbuffer, then LZ4 compress into pDest
	constexpr int64_t kiMaxBytesPerItem = 98;
	constexpr int64_t kiMaxGroupHeaders = 6 * 3;
	int64_t iMaxSerializedSize = kiMaxGroupHeaders + iCount * kiMaxBytesPerItem;

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// Reserve space in workbuffer
	int64_t iChunks = (iMaxSerializedSize + static_cast<int64_t>(sizeof(int64_t)) - 1) / static_cast<int64_t>(sizeof(int64_t));
	for (int64_t i = 0; i < iChunks; ++i)
	{
		rWorkbuffer.PushBack<int64_t>(0);
	}
	std::span<const uint8_t> serializedSpan = rWorkbuffer.Span<uint8_t>();
	uint8_t* pSerialized = const_cast<uint8_t*>(serializedSpan.data());

	int64_t iSerializedSize = SerializeStatusChangeBatch(pChanges, iCount, pSerialized, iMaxSerializedSize);

	// Write 4-byte uncompressed size prefix, then LZ4 compressed data
	uint8_t* pOutput = static_cast<uint8_t*>(pDest);
	int32_t iUncompressedSize = static_cast<int32_t>(iSerializedSize);
	std::memcpy(pOutput, &iUncompressedSize, sizeof(int32_t));

	int iCompressedSize = LZ4_compress_default(
		reinterpret_cast<const char*>(pSerialized),
		reinterpret_cast<char*>(pOutput + sizeof(int32_t)),
		static_cast<int>(iSerializedSize),
		static_cast<int>(iDestCapacity - sizeof(int32_t)));

	rWorkbuffer.Pop();

	return sizeof(int32_t) + iCompressedSize;
}

int64_t DecompressStatusChangeBatch(const void* pSource, int64_t iSourceSize, game::StatusChange* pDest, int64_t iMaxCount)
{
	if (iSourceSize <= static_cast<int64_t>(sizeof(int32_t)))
	{
		return 0;
	}

	// Read uncompressed size prefix
	const uint8_t* pInput = static_cast<const uint8_t*>(pSource);
	int32_t iUncompressedSize;
	std::memcpy(&iUncompressedSize, pInput, sizeof(int32_t));

	// Decompress into workbuffer
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	int64_t iChunks = (iUncompressedSize + static_cast<int64_t>(sizeof(int64_t)) - 1) / static_cast<int64_t>(sizeof(int64_t));
	for (int64_t i = 0; i < iChunks; ++i)
	{
		rWorkbuffer.PushBack<int64_t>(0);
	}
	std::span<const uint8_t> decompressedSpan = rWorkbuffer.Span<uint8_t>();
	uint8_t* pDecompressed = const_cast<uint8_t*>(decompressedSpan.data());

	int iResult = LZ4_decompress_safe(
		reinterpret_cast<const char*>(pInput + sizeof(int32_t)),
		reinterpret_cast<char*>(pDecompressed),
		static_cast<int>(iSourceSize - sizeof(int32_t)),
		iUncompressedSize);

	if (iResult <= 0)
	{
		common::Log("DecompressStatusChangeBatch: LZ4 decompression failed (error={})", iResult);
		rWorkbuffer.Pop();
		return 0;
	}

	int64_t iCount = DeserializeStatusChangeBatch(pDecompressed, iResult, pDest, iMaxCount);

	rWorkbuffer.Pop();

	return iCount;
}

} // namespace engine

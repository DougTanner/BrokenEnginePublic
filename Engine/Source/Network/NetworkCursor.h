#pragma once

#include "Frame/GridCoord.h"

namespace engine
{

// Cursor read helpers (shared across Network*.cpp files)
inline void ReadBytes(const uint8_t*& pCursor, void* pDest, int64_t iSize)
{
	std::memcpy(pDest, pCursor, iSize);
	pCursor += iSize;
}

inline uint8_t ReadUint8(const uint8_t*& pCursor)
{
	return *pCursor++;
}

inline uint16_t ReadUint16(const uint8_t*& pCursor)
{
	uint16_t ui = 0;
	ReadBytes(pCursor, &ui, sizeof(uint16_t));
	return ui;
}

inline int32_t ReadInt32(const uint8_t*& pCursor)
{
	int32_t i = 0;
	ReadBytes(pCursor, &i, sizeof(int32_t));
	return i;
}

inline uint32_t ReadUint32(const uint8_t*& pCursor)
{
	uint32_t ui = 0;
	ReadBytes(pCursor, &ui, sizeof(uint32_t));
	return ui;
}

inline int64_t ReadInt64(const uint8_t*& pCursor)
{
	int64_t i = 0;
	ReadBytes(pCursor, &i, sizeof(int64_t));
	return i;
}

inline uint64_t ReadUint64(const uint8_t*& pCursor)
{
	uint64_t ui = 0;
	ReadBytes(pCursor, &ui, sizeof(uint64_t));
	return ui;
}

inline float ReadFloat(const uint8_t*& pCursor)
{
	float f = 0.0f;
	ReadBytes(pCursor, &f, sizeof(float));
	return f;
}

inline XMVECTOR ReadVec4(const uint8_t*& pCursor)
{
	XMFLOAT4A f4 {};
	ReadBytes(pCursor, &f4, sizeof(XMFLOAT4A));
	return XMLoadFloat4A(&f4);
}

inline GridCoord ReadGridCoord(const uint8_t*& pCursor)
{
	GridCoord coord {};
	coord.x = ReadInt32(pCursor);
	coord.y = ReadInt32(pCursor);
	return coord;
}

// Bounds-tracking wrapper over the unchecked read helpers above for variable-length payloads:
// callers verify Has()/Remaining() before passing pCursor to the Read* helpers
struct BoundedCursor
{
	const uint8_t* pCursor {nullptr};
	const uint8_t* pEnd {nullptr};

	int64_t Remaining() const
	{
		return pEnd - pCursor;
	}

	bool Has(int64_t iBytes) const
	{
		return Remaining() >= iBytes;
	}
};

// Cursor write helpers (shared across Network*.cpp files)
inline void WriteBytes(uint8_t*& pCursor, const void* pData, int64_t iSize)
{
	std::memcpy(pCursor, pData, iSize);
	pCursor += iSize;
}

inline void WriteUint8(uint8_t*& pCursor, uint8_t ui)
{
	*pCursor++ = ui;
}

inline void WriteUint16(uint8_t*& pCursor, uint16_t ui)
{
	WriteBytes(pCursor, &ui, sizeof(uint16_t));
}

inline void WriteInt32(uint8_t*& pCursor, int32_t i)
{
	WriteBytes(pCursor, &i, sizeof(int32_t));
}

inline void WriteUint32(uint8_t*& pCursor, uint32_t ui)
{
	WriteBytes(pCursor, &ui, sizeof(uint32_t));
}

inline void WriteInt64(uint8_t*& pCursor, int64_t i)
{
	WriteBytes(pCursor, &i, sizeof(int64_t));
}

inline void WriteUint64(uint8_t*& pCursor, uint64_t ui)
{
	WriteBytes(pCursor, &ui, sizeof(uint64_t));
}

inline void WriteFloat(uint8_t*& pCursor, float f)
{
	WriteBytes(pCursor, &f, sizeof(float));
}

inline void WriteVec4(uint8_t*& pCursor, XMVECTOR vec)
{
	XMFLOAT4A f4 {};
	XMStoreFloat4A(&f4, vec);
	WriteBytes(pCursor, &f4, sizeof(XMFLOAT4A));
}

inline void WriteGridCoord(uint8_t*& pCursor, GridCoord coord)
{
	WriteInt32(pCursor, coord.x);
	WriteInt32(pCursor, coord.y);
}

inline void WriteGridCoord(common::Workbuffer& rWorkbuffer, GridCoord coord)
{
	rWorkbuffer.PushBack<int32_t>(coord.x);
	rWorkbuffer.PushBack<int32_t>(coord.y);
}

template <typename T>
inline void PushSimplePacketArg(common::Workbuffer& rWorkbuffer, const T& arg)
{
	if constexpr (std::is_same_v<T, GridCoord>)
	{
		WriteGridCoord(rWorkbuffer, arg);
	}
	else
	{
		static_assert(std::is_arithmetic_v<T>, "SendSimplePacket only supports arithmetic types and GridCoord; unwrap enums/ids/flags at the call site");
		rWorkbuffer.PushBack<T>(arg);
	}
}

} // namespace engine

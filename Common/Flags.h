#pragma once

namespace common
{

template <typename ENUM_TYPE>
class Flags
{
	using underlying_t = typename std::underlying_type<ENUM_TYPE>::type;
	static_assert(std::is_unsigned_v<underlying_t>);

public:

	constexpr Flags() = default;

	constexpr Flags(ENUM_TYPE eFlag)
	{
		Set(eFlag);
	}

	constexpr Flags(const std::initializer_list<ENUM_TYPE>& rInitialFlags)
	{
		for (ENUM_TYPE eFlag : rInitialFlags)
		{
			Set(eFlag);
		}
	}

	constexpr Flags(const Flags& rFlags) = default;
	constexpr Flags(Flags&& rFlags) noexcept = default;
	constexpr Flags& operator=(const Flags& rFlags) noexcept = default;
	constexpr Flags& operator=(Flags&& rFlags) noexcept = default;

	constexpr bool Empty() const
	{
		return std::to_underlying(meFlags) == 0;
	}

	constexpr void Set(ENUM_TYPE eFlag, bool bSet = true)
	{
		underlying_t iFlag = std::to_underlying(eFlag);
		underlying_t iCurrent = std::to_underlying(meFlags);
		bSet ? iCurrent |= iFlag : iCurrent &= ~iFlag;
		meFlags = static_cast<ENUM_TYPE>(iCurrent);
	}

	constexpr void Set(const std::initializer_list<ENUM_TYPE>& rInitialFlags)
	{
		for (ENUM_TYPE eFlag : rInitialFlags)
		{
			Set(eFlag);
		}
	}

	constexpr void Clear(ENUM_TYPE eFlag)
	{
		Set(eFlag, false);
	}

	constexpr void Clear(const std::initializer_list<ENUM_TYPE>& rInitialFlags)
	{
		for (ENUM_TYPE eFlag : rInitialFlags)
		{
			Clear(eFlag);
		}
	}

	constexpr void ClearAll()
	{
		meFlags = static_cast<ENUM_TYPE>(0);
	}

	// Composite enumerators use any-bit semantics: true when at least one requested bit is set.
	constexpr bool operator&(ENUM_TYPE eFlag) const
	{
		underlying_t iFlag = std::to_underlying(eFlag);
		return (std::to_underlying(meFlags) & iFlag) != 0;
	}

	constexpr underlying_t Mask(const Flags& rOther) const
	{
		return std::to_underlying(meFlags) & std::to_underlying(rOther.meFlags);
	}

	// For composite enumerators, returns true when any toggled bit remains set.
	constexpr bool Toggle(ENUM_TYPE eFlag)
	{
		underlying_t iFlag = std::to_underlying(eFlag);
		underlying_t iCurrent = std::to_underlying(meFlags) ^ iFlag;
		meFlags = static_cast<ENUM_TYPE>(iCurrent);
		return (iCurrent & iFlag) != 0;
	}

	auto operator<=>(const Flags&) const = default;

	inline void Write(std::ostream& rStream) const
	{
		common::Write(rStream, std::to_underlying(meFlags));
	}

	inline void Read(std::istream& rStream)
	{
		underlying_t iValue;
		common::Read(rStream, iValue);
		meFlags = static_cast<ENUM_TYPE>(iValue);
	}

	ENUM_TYPE meFlags = static_cast<ENUM_TYPE>(0);
};

} // namespace common

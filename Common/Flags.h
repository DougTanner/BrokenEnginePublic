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

	Flags(ENUM_TYPE eFlag)
	{
		*this |= eFlag;
	}

	Flags(const std::initializer_list<ENUM_TYPE>& rInitialFlags)
	{
		for (ENUM_TYPE eFlag : rInitialFlags)
		{
			*this |= eFlag;
		}
	}

	Flags(const Flags& rFlags) = default;
	Flags(Flags&& rFlags) noexcept = default;
	Flags& operator=(const Flags& rFlags) noexcept = default;
	Flags& operator=(Flags&& rFlags) noexcept = default;

	bool Empty()
	{
		return std::to_underlying(meFlags) == 0;
	}

	void Set(ENUM_TYPE eFlag, bool bSet = true)
	{
		underlying_t iFlag = std::to_underlying(eFlag);
		underlying_t iCurrent = std::to_underlying(meFlags);
		bSet ? iCurrent |= iFlag : iCurrent &= ~iFlag;
		meFlags = static_cast<ENUM_TYPE>(iCurrent);
	}

	void Clear(ENUM_TYPE eFlag)
	{
		Set(eFlag, false);
	}

	void Clear(const std::initializer_list<ENUM_TYPE>& rInitialFlags)
	{
		for (ENUM_TYPE eFlag : rInitialFlags)
		{
			underlying_t iFlag = std::to_underlying(eFlag);
			underlying_t iCurrent = std::to_underlying(meFlags);
			iCurrent &= ~iFlag;
			meFlags = static_cast<ENUM_TYPE>(iCurrent);
		}
	}

	void ClearAll()
	{
		meFlags = static_cast<ENUM_TYPE>(0);
	}

	bool operator&(ENUM_TYPE eFlag) const
	{
		underlying_t iFlag = std::to_underlying(eFlag);
		return (std::to_underlying(meFlags) & iFlag) != 0;
	}

	void operator|=(const Flags& rOther)
	{
		meFlags = static_cast<ENUM_TYPE>(std::to_underlying(meFlags) | std::to_underlying(rOther.meFlags));
	}

	void operator|=(ENUM_TYPE eFlag)
	{
		underlying_t iFlag = std::to_underlying(eFlag);
		meFlags = static_cast<ENUM_TYPE>(std::to_underlying(meFlags) | iFlag);
	}

	underlying_t operator&(const Flags& rOther) const
	{
		return std::to_underlying(meFlags) & std::to_underlying(rOther.meFlags);
	}

	bool Toggle(ENUM_TYPE eFlag)
	{
		underlying_t iFlag = std::to_underlying(eFlag);
		underlying_t iCurrent = std::to_underlying(meFlags);

		if ((iCurrent & iFlag) == iFlag)
		{
			iCurrent &= ~iFlag;
		}
		else if ((iCurrent & iFlag) == 0)
		{
			iCurrent |= iFlag;
		}
		else
		{
			DEBUG_BREAK();
		}

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

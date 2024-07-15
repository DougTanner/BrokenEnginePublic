#pragma once

namespace common
{

template<typename ENUM_TYPE>
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
		for (const ENUM_TYPE& reFlag : rInitialFlags)
		{
			*this |= reFlag;
		}
	}

	Flags(const Flags& rFlags) = default;
	Flags(Flags&& rFlags) noexcept = default;
	Flags& operator=(const Flags& rFlags) noexcept = default;
	Flags& operator=(Flags&& rFlags) noexcept = default;

	void Set(ENUM_TYPE eFlag, bool bSet)
	{
		underlying_t iFlag = static_cast<underlying_t>(eFlag);
		bSet ? muiUnderlying |= iFlag : muiUnderlying &= ~iFlag;
	}

	void Clear(ENUM_TYPE eFlag)
	{
		Set(eFlag, false);
	}

	void Clear(const std::initializer_list<ENUM_TYPE>& rInitialFlags)
	{
		for (const ENUM_TYPE& reFlag : rInitialFlags)
		{
			*this &= reFlag;
		}
	}

	void ClearAll()
	{
		muiUnderlying = 0;
	}

	bool operator&(ENUM_TYPE eFlag) const
	{
		underlying_t iFlag = static_cast<underlying_t>(eFlag);
		return (muiUnderlying & iFlag) != 0;
	}

	void operator|=(const Flags& rOther)
	{
		muiUnderlying |= rOther.muiUnderlying;
	}

	void operator|=(ENUM_TYPE eFlag)
	{
		underlying_t iFlag = static_cast<underlying_t>(eFlag);
		muiUnderlying |= iFlag;
	}

	void operator&=(ENUM_TYPE eFlag)
	{
		underlying_t iFlag = static_cast<underlying_t>(eFlag);
		muiUnderlying &= ~iFlag;
	}

	underlying_t operator&(const Flags& rOther) const
	{
		return static_cast<underlying_t>(muiUnderlying) & static_cast<underlying_t>(rOther.muiUnderlying);
	}

	bool Toggle(ENUM_TYPE eFlag)
	{
		underlying_t iFlag = static_cast<underlying_t>(eFlag);

		if ((muiUnderlying & iFlag) == iFlag)
		{
			muiUnderlying &= ~iFlag;
		}
		else if ((muiUnderlying & iFlag) == 0)
		{
			muiUnderlying |= iFlag;
		}
		else
		{
			DEBUG_BREAK();
		}

		return (muiUnderlying & iFlag) != 0;
	}

	auto operator<=>(const Flags&) const = default;

	underlying_t muiUnderlying = 0;
};

} // namespace common

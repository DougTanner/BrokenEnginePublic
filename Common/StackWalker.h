#pragma once

namespace common
{

#if defined(BT_ENGINE)

class LogStackWalker : public StackWalker
{
public:
	LogStackWalker(ExceptType eExceptType)
	: StackWalker(eExceptType)
	{
	}

protected:

	void OnSymInit([[maybe_unused]] LPCSTR szSearchPath, [[maybe_unused]] DWORD symOptions, [[maybe_unused]] LPCSTR szUserName) override
	{
	}

	void OnLoadModule([[maybe_unused]] LPCSTR img, [[maybe_unused]] LPCSTR mod, [[maybe_unused]] DWORD64 baseAddr, [[maybe_unused]] DWORD size, [[maybe_unused]] DWORD result, [[maybe_unused]] LPCSTR symType, [[maybe_unused]] LPCSTR pdbName, [[maybe_unused]] ULONGLONG fileVersion) override
	{
	}

	void OnCallstackEntry([[maybe_unused]] CallstackEntryType eType, CallstackEntry& entry) override
	{
		if (entry.lineNumber > 0)
		{
			Log("{} | {} | {}", entry.name, entry.lineNumber, entry.lineFileName);
		}
	}

	void OnDbgHelpErr([[maybe_unused]] LPCSTR szFuncName, [[maybe_unused]] DWORD gle, [[maybe_unused]] DWORD64 addr) override
	{
	}

	void OnOutput([[maybe_unused]] LPCSTR szText) override
	{
	}
};

class OfstreamStackWalker : public StackWalker
{
public:
	OfstreamStackWalker(ExceptType eExceptType, std::ofstream* pOfstream)
	: StackWalker(eExceptType)
	, mpOfstream(pOfstream)
	{
	}

protected:

	void OnSymInit([[maybe_unused]] LPCSTR szSearchPath, [[maybe_unused]] DWORD symOptions, [[maybe_unused]] LPCSTR szUserName) override
	{
	}

	void OnLoadModule([[maybe_unused]] LPCSTR img, [[maybe_unused]] LPCSTR mod, [[maybe_unused]] DWORD64 baseAddr, [[maybe_unused]] DWORD size, [[maybe_unused]] DWORD result, [[maybe_unused]] LPCSTR symType, [[maybe_unused]] LPCSTR pdbName, [[maybe_unused]] ULONGLONG fileVersion) override
	{
	}

	void OnCallstackEntry([[maybe_unused]] CallstackEntryType eType, CallstackEntry& entry) override
	{
		if (entry.lineNumber > 0)
		{
			*mpOfstream << entry.name << " | " << entry.lineNumber << " | " << entry.lineFileName << std::endl << std::flush;
		}
	}

	void OnDbgHelpErr([[maybe_unused]] LPCSTR szFuncName, [[maybe_unused]] DWORD gle, [[maybe_unused]] DWORD64 addr) override
	{
	}

	void OnOutput([[maybe_unused]] LPCSTR szText) override
	{
	}

	std::ofstream* mpOfstream = nullptr;
};

#endif

} // namespace common

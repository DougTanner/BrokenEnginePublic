#pragma once

namespace common
{

class LogStackWalker : public StackWalker
{
public:
	LogStackWalker(ExceptType eExceptType)
	: StackWalker(eExceptType)
	{
	}

protected:

	void OnSymInit([[maybe_unused]] LPCSTR searchPath, [[maybe_unused]] DWORD symOptions, [[maybe_unused]] LPCSTR userName) override
	{
	}

	void OnLoadModule([[maybe_unused]] LPCSTR imagePath, [[maybe_unused]] LPCSTR moduleName, [[maybe_unused]] DWORD64 baseAddr, [[maybe_unused]] DWORD size, [[maybe_unused]] DWORD result, [[maybe_unused]] LPCSTR symType, [[maybe_unused]] LPCSTR pdbName, [[maybe_unused]] ULONGLONG fileVersion) override
	{
	}

	void OnCallstackEntry([[maybe_unused]] CallstackEntryType eType, CallstackEntry& rEntry) override
	{
		if (rEntry.lineNumber > 0)
		{
			LOG(kDefault, kError, "{} | {} | {}", rEntry.name, rEntry.lineNumber, rEntry.lineFileName);
		}
	}

	void OnDbgHelpErr([[maybe_unused]] LPCSTR funcName, [[maybe_unused]] DWORD lastError, [[maybe_unused]] DWORD64 addr) override
	{
	}

	void OnOutput([[maybe_unused]] LPCSTR text) override
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

	void OnSymInit([[maybe_unused]] LPCSTR searchPath, [[maybe_unused]] DWORD symOptions, [[maybe_unused]] LPCSTR userName) override
	{
	}

	void OnLoadModule([[maybe_unused]] LPCSTR imagePath, [[maybe_unused]] LPCSTR moduleName, [[maybe_unused]] DWORD64 baseAddr, [[maybe_unused]] DWORD size, [[maybe_unused]] DWORD result, [[maybe_unused]] LPCSTR symType, [[maybe_unused]] LPCSTR pdbName, [[maybe_unused]] ULONGLONG fileVersion) override
	{
	}

	void OnCallstackEntry([[maybe_unused]] CallstackEntryType eType, CallstackEntry& rEntry) override
	{
		if (rEntry.lineNumber > 0)
		{
			*mpOfstream << rEntry.name << " | " << rEntry.lineNumber << " | " << rEntry.lineFileName << std::endl << std::flush;
		}
	}

	void OnDbgHelpErr([[maybe_unused]] LPCSTR funcName, [[maybe_unused]] DWORD lastError, [[maybe_unused]] DWORD64 addr) override
	{
	}

	void OnOutput([[maybe_unused]] LPCSTR text) override
	{
	}

	std::ofstream* mpOfstream = nullptr;
};

} // namespace common

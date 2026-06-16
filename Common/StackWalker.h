#pragma once

namespace common
{

// Shared base for the crash-path stack walkers: owns the emission gate, the DbgHelp error emit, and the
// process-wide DbgHelp serialization (dbghelp.dll is documented single-threaded per process — see
// gDbgHelpMutex in Determinism.h). Leaf classes implement only the Emit sink.
class FilteredStackWalker : public StackWalker
{
public:
	FilteredStackWalker(ExceptType eExceptType)
	: StackWalker(eExceptType)
	{
	}

	// Hides the upstream overload so every driver serializes on gDbgHelpMutex. try_to_lock: never hang a
	// crashing process on a walker wedged in another thread; recursive_mutex keeps a fault inside DbgHelp
	// during a walk well-defined (nested attempt) instead of UB/self-deadlock
	BOOL ShowCallstack()
	{
		std::unique_lock<std::recursive_mutex> uniqueLock(gDbgHelpMutex, std::try_to_lock);
		if (!uniqueLock.owns_lock())
		{
			Emit("<callstack skipped - DbgHelp busy>");
			return FALSE;
		}

		return StackWalker::ShowCallstack();
	}

protected:

	virtual void Emit(const char* pcText) = 0;

	void OnSymInit([[maybe_unused]] LPCSTR searchPath, [[maybe_unused]] DWORD symOptions, [[maybe_unused]] LPCSTR userName) override
	{
	}

	void OnLoadModule([[maybe_unused]] LPCSTR imagePath, [[maybe_unused]] LPCSTR moduleName, [[maybe_unused]] DWORD64 baseAddr, [[maybe_unused]] DWORD size, [[maybe_unused]] DWORD result, [[maybe_unused]] LPCSTR symType, [[maybe_unused]] LPCSTR pdbName, [[maybe_unused]] ULONGLONG fileVersion) override
	{
	}

	void OnCallstackEntry(CallstackEntryType eType, CallstackEntry& rEntry) override
	{
		// Upstream re-delivers the final frame with eType == lastEntry after its regular emit — skip the duplicate.
		// _TRUNCATE: a sprintf_s overflow would invoke the throwing invalid-parameter handler on this fault path
		if (eType == lastEntry || rEntry.name[0] == '\0')
		{
			return;
		}

		char pcLine[2048] {};
		if (rEntry.lineNumber > 0)
		{
			_snprintf_s(pcLine, std::size(pcLine), _TRUNCATE, "%s | %lu | %s", rEntry.name, rEntry.lineNumber, rEntry.lineFileName);
		}
		else
		{
			// Named frame with no line info (release PDB / system DLL / inlined) — emit offset form instead of dropping
			_snprintf_s(pcLine, std::size(pcLine), _TRUNCATE, "%s + 0x%llX | %s", rEntry.name, rEntry.offsetFromSymbol, rEntry.moduleName);
		}

		Emit(pcLine);
	}

	void OnDbgHelpErr(LPCSTR funcName, DWORD lastError, DWORD64 addr) override
	{
		// Per-frame line-lookup failures are expected with line-stripped PDBs and already visible as offset-form frames
		if (std::strcmp(funcName, "SymGetLineFromAddr64") == 0 || std::strcmp(funcName, "SymGetLineFromInlineContext") == 0)
		{
			return;
		}

		char pcLine[1024] {};
		_snprintf_s(pcLine, std::size(pcLine), _TRUNCATE, "DbgHelp error: %s | %lu | 0x%llX", funcName, lastError, addr);
		Emit(pcLine);
	}

	void OnOutput([[maybe_unused]] LPCSTR text) override
	{
	}
};

class LogStackWalker : public FilteredStackWalker
{
public:
	LogStackWalker(ExceptType eExceptType)
	: FilteredStackWalker(eExceptType)
	{
	}

protected:

	void Emit(const char* pcText) override
	{
		// Fault-path emit: relies on Log()'s thread_local fallback buffer (GetLogFallbackBuffer) for race-freedom on
		// ThreadLocal-less faulting threads, and stays allocation-free. Do not reintroduce a shared static there.
		LOG(kDefault, kError, "{}", pcText);
	}
};

class OfstreamStackWalker : public FilteredStackWalker
{
public:
	OfstreamStackWalker(ExceptType eExceptType, std::ofstream* pOfstream)
	: FilteredStackWalker(eExceptType)
	, mpOfstream(pOfstream)
	{
	}

protected:

	void Emit(const char* pcText) override
	{
		*mpOfstream << pcText << std::endl;
	}

	std::ofstream* mpOfstream = nullptr;
};

} // namespace common

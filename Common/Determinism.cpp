#include "Determinism.h"

namespace common
{

// Serializes the three fault-path handlers below via try_to_lock — an un-acquirable lock means another thread is mid-handler, so the handler bails (the cross-thread guard, which is the only intended use).
// Deliberately a plain std::mutex, NOT recursive like gDbgHelpMutex: the bail-on-owned-re-entry behavior is intentional. A nested same-thread fault must NOT re-run a handler (each ends in throw, so re-running risks std::terminate). An owned re-entry technically makes try_lock formal UB ([thread.mutex.requirements.mutex]), but is benign on MSVC — the SRWLOCK-backed try_lock returns false on an already-owned lock, i.e. the desired bail. Documented-accept rather than hardened: a recursive mutex would invert the bail intent, and an explicit re-entry guard adds fault-path complexity for a hazard with no observed manifestation.
static std::mutex sMutex;

std::recursive_mutex gDbgHelpMutex;

thread_local int64_t giExpectedThrowDepth = 0;

void ConfigureThreadFloatingPoint()
{
	// Flush denormals for deterministic FP math across all threads
	unsigned int uiCurrentState = 0;
	_controlfp_s(&uiCurrentState, _DN_FLUSH | _RC_NEAR, _MCW_DN | _MCW_RC);
}

void SetupExceptionHandling()
{
	// _set_se_translator and _set_invalid_parameter_handler are per-thread CRT handler slots — install on every thread.

	// Combined with compiler flag /EHa, this allows us to trap (and re-throw as C++ exceptions) basic exceptions like nullptr dereferences
	_set_se_translator([](unsigned int uiCode, [[maybe_unused]] EXCEPTION_POINTERS* pExceptionPointers)
	{
		std::unique_lock<std::mutex> uniqueLock(sMutex, std::try_to_lock);
		if (!uniqueLock.owns_lock())
		{
			return;
		}

		LOG(kDefault, kError, "In _set_se_translator: {}", uiCode);

		DEBUG_BREAK();

		if (uiCode == 0xC0000005)
		{
			throw std::runtime_error("Access Violation");
		}

		static char spcCode[64] {};
		sprintf_s(spcCode, std::size(spcCode) - 1, "_set_se_translator 0x%08x", uiCode);
		throw std::runtime_error(spcCode);
	});

	_set_invalid_parameter_handler([](const wchar_t* pcExpression, const wchar_t* pcFunction, const wchar_t* pcFile, unsigned int uiLine, [[maybe_unused]] uintptr_t pReserved)
	{
		std::unique_lock<std::mutex> uniqueLock(sMutex, std::try_to_lock);
		if (!uniqueLock.owns_lock())
		{
			return;
		}

		LOG(kDefault, kError, "In _set_invalid_parameter_handler");

		// Fixed-buffer formatting (no heap allocation): the fault under diagnosis may be heap corruption, so re-entering the allocator could re-fault. Mirrors the SEH translator's spcCode idiom above.
		static char spcDescription[1024] {};
		sprintf_s(spcDescription, std::size(spcDescription) - 1, "_set_invalid_parameter_handler \"%ls\" Function: %ls File: %ls Line: %u", pcExpression ? pcExpression : L"nullptr", pcFunction ? pcFunction : L"nullptr", pcFile ? pcFile : L"nullptr", uiLine);

		DEBUG_BREAK();

		throw std::runtime_error(spcDescription);
	});

	// Process-global handlers install exactly once per process via std::call_once — race-free across concurrent ThreadLocal ctors.
	static std::once_flag sOnceFlag;
	std::call_once(sOnceFlag, []()
	{
		_set_error_mode(_OUT_TO_DEFAULT);
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
		_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
		_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);

		AddVectoredExceptionHandler(1, [](PEXCEPTION_POINTERS pExceptionPointers) -> LONG
		{
			if (pExceptionPointers == nullptr || pExceptionPointers->ExceptionRecord == nullptr) [[unlikely]]
			{
				return EXCEPTION_CONTINUE_SEARCH;
			}

			std::unique_lock<std::mutex> uniqueLock(sMutex, std::try_to_lock);
			if (!uniqueLock.owns_lock())
			{
				return EXCEPTION_CONTINUE_SEARCH;
			}

			DWORD uiExceptionCode = pExceptionPointers->ExceptionRecord->ExceptionCode;
			switch (uiExceptionCode)
			{
				case DBG_PRINTEXCEPTION_WIDE_C:
					LOG(kDefault, kVerbose, "DBG_PRINTEXCEPTION_WIDE_C");
					break;

				case DBG_PRINTEXCEPTION_C:
					if (giMyOutputDebugString == 0
						&& pExceptionPointers->ExceptionRecord->NumberParameters >= 2
						&& pExceptionPointers->ExceptionRecord->ExceptionInformation[1] != 0)
					{
						LOG(kDefault, kVerbose, "DBG_PRINTEXCEPTION_C: {}", reinterpret_cast<char*>(pExceptionPointers->ExceptionRecord->ExceptionInformation[1]));
					}
					break;

				case static_cast<DWORD>(RPC_E_DISCONNECTED):
					// Caused by gamepad library?
					LOG(kDefault, kWarning, "RPC_E_DISCONNECTED: The object invoked has disconnected from its clients");
					break;

				case EXCEPTION_STACK_OVERFLOW:
					LOG(kDefault, kError, "Vectored exception: Stack overflow");
					DEBUG_BREAK();
					break;

				case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
				case EXCEPTION_ACCESS_VIOLATION:
				case EXCEPTION_FLT_DIVIDE_BY_ZERO:
				case EXCEPTION_FLT_OVERFLOW:
				case EXCEPTION_INT_DIVIDE_BY_ZERO:
				case EXCEPTION_INT_OVERFLOW:
				case EXCEPTION_IN_PAGE_ERROR:
				case EXCEPTION_DATATYPE_MISALIGNMENT:
				case EXCEPTION_ILLEGAL_INSTRUCTION:
				case STATUS_HEAP_CORRUPTION:
				case 0xE06D7363: // Microsoft C++ SEH Exception
				{
					// Expected-throw region: a designed error path (e.g. agent command validation) throws to reach its catch — skip crash diagnostics for the C++ throw only.
					if (uiExceptionCode == 0xE06D7363 && giExpectedThrowDepth > 0)
					{
						return EXCEPTION_CONTINUE_SEARCH;
					}

					char pcHex[20] {};
					LOG(kDefault, kError, "Vectored exception: {}", ToHex(std::span(pcHex), uiExceptionCode));

					bool bDxDiagThread = gpThreadLocal != nullptr && gpThreadLocal->miThreadId.has_value() && gpThreadLocal->miThreadId.value() == kThreadDxDiag;
					if (bDxDiagThread)
					{
						LOG(kDefault, kError, "  bDxDiagThread");
					}
					else
					{
						const char* pcType = uiExceptionCode == 0xC0000374 ? "Heap corruption" : (uiExceptionCode == 0xC0000005 ? "Access Violation" : (uiExceptionCode == 0xE06D7363 ? "Microsoft C++ SEH Exception" : "Unknown type"));
						LOG(kDefault, kError, "<{}>", pcType);
						LogStackWalker logStackWalker(StackWalker::NonExcept);
						logStackWalker.ShowCallstack();
						LOG(kDefault, kError, "</ {}>", pcType);

						DEBUG_BREAK();
					}

					break;
				}

				default:
					LOG(kDefault, kError, "Unhandled vectored exception: {}", uiExceptionCode);
					DEBUG_BREAK();
					break;
			}

			return EXCEPTION_CONTINUE_SEARCH;
		});

		std::set_terminate([]()
		{
			LOG(kDefault, kError, "std::set_terminate");
			DEBUG_BREAK();
			std::abort();
		});
	});
}

} // namespace common

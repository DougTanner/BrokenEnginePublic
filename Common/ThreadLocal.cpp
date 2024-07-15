#include "ThreadLocal.h"

namespace common
{

static std::mutex sMutex;

void SetupExceptionHandling()
{
	_set_error_mode(_OUT_TO_DEFAULT);
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);

	// Combined with compiler flag /EHa, this allows us to trap (and re-throw as C++ exceptions) basic exceptions like nullptr dereferences
	_set_se_translator([](unsigned int uiCode, [[maybe_unused]] EXCEPTION_POINTERS* pExceptionPointers)
	{
		std::unique_lock<std::mutex> uniqueLock(sMutex, std::try_to_lock);
		if (!uniqueLock.owns_lock())
		{
			return;
		}

		Log("In _set_se_translator: {}", uiCode);
	
		DEBUG_BREAK();

		if (uiCode == 0xC0000005)
		{
			throw std::exception("Access Violation");
		}

		static char spcCode[64] {};
		sprintf_s(spcCode, std::size(spcCode) - 1, "_set_se_translator 0x%08x", uiCode);
		throw std::exception(spcCode);
	});

	_set_invalid_parameter_handler([](const wchar_t* pcExpression, const wchar_t* pcFunction, const wchar_t* pcFile, unsigned int uiLine, [[maybe_unused]] uintptr_t pReserved)
	{
		std::unique_lock<std::mutex> uniqueLock(sMutex, std::try_to_lock);
		if (!uniqueLock.owns_lock())
		{
			return;
		}

		Log("In _set_invalid_parameter_handler");
	
		std::wstring description(L"_set_invalid_parameter_handler \"");
		description += pcExpression ? pcExpression : L"nullptr";
		description += L"\" Function: ";
		description += pcFunction ? pcFunction : L"nullptr";
		description += L" File: ";
		description += pcFile ? pcFile : L"nullptr";
		description += L" Line: ";
		description += std::to_wstring(uiLine);
	
		DEBUG_BREAK();

		throw std::exception(ToString(description).c_str());
	});

	static bool sbDone = false;
	if (!sbDone)
	{
		sbDone = true;

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
					Log("DBG_PRINTEXCEPTION_WIDE_C");
					break;

				case DBG_PRINTEXCEPTION_C:
					if (giMyOutputDebugString == 0)
					{
						Log("DBG_PRINTEXCEPTION_C: {}", reinterpret_cast<char*>(pExceptionPointers->ExceptionRecord->ExceptionInformation[1]));
					}
					break;

				case RPC_E_DISCONNECTED:
					// Caused by gamepad library?
					LOG("RPC_E_DISCONNECTED: The object invoked has disconnected from its clients");
					break;

				case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
				case EXCEPTION_ACCESS_VIOLATION:
				case EXCEPTION_FLT_DIVIDE_BY_ZERO:
				case EXCEPTION_FLT_OVERFLOW:
				case EXCEPTION_INT_DIVIDE_BY_ZERO:
				case EXCEPTION_INT_OVERFLOW:
				case EXCEPTION_IN_PAGE_ERROR:
				case EXCEPTION_STACK_OVERFLOW:
				case EXCEPTION_DATATYPE_MISALIGNMENT:
				case EXCEPTION_ILLEGAL_INSTRUCTION:
				case STATUS_HEAP_CORRUPTION:
				case 0xE06D7363: // Microsoft C++ SEH Exception
				{
					Log("Vectored exception: 0x{:X}", uiExceptionCode);

					bool bDxDiagThread = gpThreadLocal != nullptr && gpThreadLocal->miThreadId.has_value() && gpThreadLocal->miThreadId.value() == kThreadDxDiag;
					if (bDxDiagThread)
					{
						Log("  bDxDiagThread");
					}
					else
					{
						const char* pcType = uiExceptionCode == 0xC0000374 ? "Heap corruption" : (uiExceptionCode == 0xC0000005 ? "Access Violation" : (uiExceptionCode == 0xE06D7363 ? "Microsoft C++ SEH Exception" : "Unknown type"));
						Log("<{}>", pcType);
						LogStackWalker logStackWalker(StackWalker::NonExcept);
						logStackWalker.ShowCallstack();
						Log("</ {}>", pcType);

						DEBUG_BREAK();
					}

					break;
				}

				default:
					Log("Unhandled vectored exception: {}", uiExceptionCode);
					DEBUG_BREAK();
					break;
			}

			return EXCEPTION_CONTINUE_SEARCH;
		});
	}

	std::set_terminate([]()
	{
		Log("std::set_terminate");
		DEBUG_BREAK();
		std::abort();
	});
}

} // namespace common

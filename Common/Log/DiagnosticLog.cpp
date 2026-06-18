#include "DiagnosticLog.h"

namespace common
{

DiagnosticLog::DiagnosticLog(int iIndex, const char* pcFilename)
: mFile((std::filesystem::create_directories(std::filesystem::path(pcFilename).parent_path()), pcFilename), std::ios::out | std::ios::trunc)
, miIndex(iIndex)
{
	ASSERT(gpDiagnosticLogs[miIndex] == nullptr);

	gpDiagnosticLogs[miIndex] = this;
}

DiagnosticLog::~DiagnosticLog()
{
	if (gpDiagnosticLogs[miIndex] == this)
	{
		gpDiagnosticLogs[miIndex] = nullptr;
	}
}

} // namespace common

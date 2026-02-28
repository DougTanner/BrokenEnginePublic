#include "DiagnosticLog.h"

namespace common
{

DiagnosticLog::DiagnosticLog(int iIndex, const char* pcFilename)
: mFile((std::filesystem::create_directories(std::filesystem::path(pcFilename).parent_path()), pcFilename), std::ios::out | std::ios::trunc)
, miIndex(iIndex)
{
	gpDiagnosticLogs[miIndex] = this;
}

DiagnosticLog::~DiagnosticLog()
{
	gpDiagnosticLogs[miIndex] = nullptr;
}

} // namespace common

#include "DiagnosticLog.h"

namespace common
{

DiagnosticLog::DiagnosticLog(const char* pcFilename)
: mFile(pcFilename, std::ios::out | std::ios::trunc)
{
	gpDiagnosticLog = this;
}

DiagnosticLog::~DiagnosticLog()
{
	gpDiagnosticLog = nullptr;
}

} // namespace common

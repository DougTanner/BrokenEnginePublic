#include "DiagnosticLog.h"

namespace common
{

DiagnosticLog::DiagnosticLog(const char* pcFilename)
: mFile((std::filesystem::create_directories(std::filesystem::path(pcFilename).parent_path()), pcFilename), std::ios::out | std::ios::trunc)
{
	gpDiagnosticLog = this;
}

DiagnosticLog::~DiagnosticLog()
{
	gpDiagnosticLog = nullptr;
}

} // namespace common

#pragma once

namespace common
{

// Converts the last Windows API error code to a human-readable string
// Uses GetLastError() to retrieve the error code and FormatMessage() to convert it
// Returns: std::string containing the formatted error message
// Thread-safety: Thread-safe - returns an owned string (no shared buffer)
std::string LastErrorString();

// Converts an HRESULT error code to a human-readable string
// Parameters:
//   hresult - The HRESULT error code to convert (e.g., from DirectX, COM, or Windows APIs)
// Returns: std::string containing the formatted error message
// Thread-safety: Thread-safe - returns an owned string (no shared buffer)
std::string HresultToString(HRESULT hresult);

// Converts a filesystem file time to formatted date and time strings in user's locale
// Parameters:
//   rFileTime - The std::filesystem::file_time_type to convert (typically from std::filesystem::last_write_time)
// Returns: std::tuple<std::string, std::string> containing (date, time)
//   - Date format: "yyyy-MM-dd" (e.g., "2024-01-15")
//   - Time format: "h:mm tt" (e.g., "3:45 PM")
// Converts to user's local timezone using SystemTimeToTzSpecificLocalTime()
// Thread-safety: Thread-safe - uses local stack buffers
std::tuple<std::string, std::string> FileTimeString(const std::filesystem::file_time_type& rFileTime);

struct ExecutableResult
{
	std::string mOutput;
	int64_t miExitCode = 0;
};

// Executes an external process with captured stdin/stdout/stderr and returns the output and exit code
// Parameters:
//   rExecutableFile - Full path to the executable to run
//   rCommandLine - Command-line arguments (wide string). Modified by CreateProcessW so cannot be const
// Returns: ExecutableResult containing combined stdout/stderr output and the process exit code
// Creates pipes for process communication, runs the process with CREATE_NO_WINDOW flag,
// captures all output until the process terminates or closes its output handles
// Used by DataPacker to run build tools like glslangValidator and ffmpeg
// Thread-safety: Thread-safe - uses local resources and process isolation
ExecutableResult RunExecutable(const std::filesystem::path& rExecutableFile, std::wstring& rCommandLine);

// Launches an external process without waiting for it to complete (fire-and-forget)
// Parameters:
//   rExecutableFile - Full path to the executable to run
// Creates the process with CREATE_NO_WINDOW flag and immediately closes handles
// Thread-safety: Thread-safe - uses local resources and process isolation
void LaunchExecutable(const std::filesystem::path& rExecutableFile);

// Executes an external process attached to a fresh console window and waits for it to complete.
// Used for tools like Gaea.Swarm.exe that throw "The handle is invalid" when stdin/stdout/stderr
// are pipes or files rather than real console handles. Output is NOT captured — the child writes
// to its own console — and the new console window flashes briefly during the bake.
// Parameters:
//   rExecutableFile - Full path to the executable to run
//   rCommandLine - Command-line arguments (wide string). Modified by CreateProcessW so cannot be const
// Returns: ExecutableResult; mOutput is always empty, miExitCode is the process exit code
// Thread-safety: Thread-safe - uses local resources and process isolation
ExecutableResult RunExecutableInNewConsole(const std::filesystem::path& rExecutableFile, std::wstring& rCommandLine);

// Returns the number of logical CPU cores including hyperthreading
// Uses std::thread::hardware_concurrency() to query the system
// Returns: int64_t number of logical cores (minimum 1)
//   - On hyperthreaded systems, returns physical cores × 2
//   - If hardware_concurrency() fails (returns 0), defaults to 1 and logs a warning
// Thread-safety: Thread-safe - standard library call
int64_t LogicalCoreCount();

// Returns the number of physical CPU cores excluding hyperthreading
// Uses Windows GetLogicalProcessorInformation() API to query actual hardware cores
// Returns: int64_t number of physical cores (minimum 1)
//   - Counts only cores with RelationProcessorCore relationship
//   - Falls back to LogicalCoreCount() if the API is unavailable or fails
//   - Dynamically resizes buffer if ERROR_INSUFFICIENT_BUFFER is returned
// Thread-safety: Thread-safe - uses local resources and Windows API
// Use this for determining optimal worker thread counts to avoid hyperthreading overhead
int64_t HardwareCoreCount();

} // namespace common

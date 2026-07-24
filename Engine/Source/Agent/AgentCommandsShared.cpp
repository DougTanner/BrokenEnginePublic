#include "Pch.h"

#include "Agent/AgentCommandsShared.h"

namespace engine
{

namespace
{

common::LogLevel ParseLogLevel(std::string_view name)
{
	if (name == "Verbose")
	{
		return kVerbose;
	}
	if (name == "Debug")
	{
		return kDebug;
	}
	if (name == "Info")
	{
		return kInfo;
	}
	if (name == "Warning")
	{
		return kWarning;
	}
	if (name == "Error")
	{
		return kError;
	}
	throw std::runtime_error("unknown log level");
}

const char* LogLevelName(common::LogLevel eLevel)
{
	switch (eLevel)
	{
		case kVerbose: return "Verbose";
		case kDebug: return "Debug";
		case kInfo: return "Info";
		case kWarning: return "Warning";
		case kError: return "Error";
	}
	return "Info";
}

common::LogCategory ParseLogCategory(std::string_view name)
{
	for (int64_t i = 0; i < common::kiLogCategoryCount; ++i)
	{
		if (name == common::kpcLogCategoryNames[i])
		{
			return static_cast<common::LogCategory>(i);
		}
	}
	throw std::runtime_error("unknown log category");
}

// Collects up to iCount log lines from rBuffer into rLines. When bPattern, the whole buffer is scanned and the
// regex is applied per line before the count limit, so the last iCount *matching* lines are returned (chronological).
template <typename BUFFER>
void CollectLogLines(const BUFFER& rBuffer, int64_t iCount, bool bPattern, const std::regex& rPattern, nlohmann::json& rLines)
{
	_Analysis_assume_(iCount >= 0);
	const char* pLines[BUFFER::kiLineCount] {};
	int64_t iScan = bPattern ? BUFFER::kiLineCount : iCount;
	int64_t iFilled = rBuffer.Tail(pLines, iScan);
	_Analysis_assume_(iFilled >= 0 && iFilled <= BUFFER::kiLineCount);

	const char* pMatching[BUFFER::kiLineCount] {};
	int64_t iMatchCount = 0;
	for (int64_t i = 0; i < iFilled; ++i)
	{
		_Analysis_assume_(pLines[i] != nullptr);
		if (!bPattern || std::regex_search(pLines[i], rPattern))
		{
			pMatching[iMatchCount++] = pLines[i];
		}
	}

	int64_t iStart = std::max<int64_t>(0, iMatchCount - iCount);
	_Analysis_assume_(iStart >= 0 && iStart <= iMatchCount && iMatchCount <= BUFFER::kiLineCount);
	for (int64_t i = iStart; i < iMatchCount; ++i)
	{
		_Analysis_assume_(pMatching[i] != nullptr);
		std::string_view line(pMatching[i]);
		if (!line.empty() && line.back() == '\n')
		{
			line.remove_suffix(1);
		}
		rLines.push_back(std::string(line));
	}
}

void CommandPing([[maybe_unused]] const nlohmann::json& rParams, nlohmann::json& rResult, int64_t iGameTick)
{
#if defined(BT_CLIENT)
	rResult["build"] = "client";
#else
	rResult["build"] = "server";
#endif
	rResult["tick"] = iGameTick;
}

void CommandQuit([[maybe_unused]] const nlohmann::json& rParams, nlohmann::json& rResult)
{
	rResult = nlohmann::json::object();
	engine::RequestQuit();
}

void CommandGetLogs(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	// .contains() is null-safe (false for a non-object params); .at().get() throws on a mistyped count (designed error path).
	int64_t iCount = rParams.contains("count") ? rParams.at("count").get<int64_t>() : 64;
	if (iCount < 0)
	{
		iCount = 0;
	}

	bool bPattern = rParams.contains("pattern");
	std::regex pattern;
	if (bPattern)
	{
		try
		{
			pattern.assign(rParams.at("pattern").get<std::string>(), std::regex::ECMAScript);
		}
		catch (const std::regex_error&)
		{
			throw std::runtime_error("invalid regex pattern");
		}
	}

	nlohmann::json lines = nlohmann::json::array();
	if (rParams.contains("category"))
	{
		common::LogCategory eCategory = ParseLogCategory(rParams.at("category").get<std::string>());
		CollectLogLines(common::gLogRingBuffers[static_cast<int64_t>(eCategory)], iCount, bPattern, pattern, lines);
	}
	else
	{
		// The wrapping cross-category ring — always the most recent lines. gLogGlobalBuffer freezes after its capacity and feeds the crash dump, so it is unsuitable for a live tail.
		CollectLogLines(common::gLogAgentBuffer, iCount, bPattern, pattern, lines);
	}

	rResult["lines"] = std::move(lines);
}

void CommandSetLogLevel(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if (!rParams.contains("level"))
	{
		throw std::runtime_error("set_log_level requires 'level'");
	}
	common::LogLevel eLevel = ParseLogLevel(rParams.at("level").get<std::string>());

	rResult = nlohmann::json::object();

	// LOG() compile-eliminates any line below a category's compile-time floor (keLogLevels), so storing a runtime
	// level below that floor can never actually emit. Clamp to the floor and report the effective level(s) back.
	auto applyClamped = [eLevel](common::LogCategory eCategory) -> common::LogLevel
	{
		common::LogLevel eFloor = keLogLevels[static_cast<int64_t>(eCategory)];
		common::LogLevel eEffective = eLevel < eFloor ? eFloor : eLevel;
		common::SetLogRuntimeLevel(eCategory, eEffective);
		return eEffective;
	};

	if (rParams.contains("category"))
	{
		common::LogCategory eCategory = ParseLogCategory(rParams.at("category").get<std::string>());
		rResult["effective"] = LogLevelName(applyClamped(eCategory));
	}
	else
	{
		nlohmann::json effective = nlohmann::json::object();
		for (int64_t i = 0; i < common::kiLogCategoryCount; ++i)
			effective[common::kpcLogCategoryNames[i]] = LogLevelName(applyClamped(static_cast<common::LogCategory>(i)));
		rResult["effective"] = std::move(effective);
	}
}

} // namespace

bool ExecuteSharedAgentCommand(std::string_view cmd, const nlohmann::json& rParams, nlohmann::json& rResult, int64_t iGameTick)
{
	if (cmd == "ping")
	{
		CommandPing(rParams, rResult, iGameTick);
	}
	else if (cmd == "quit")
	{
		CommandQuit(rParams, rResult);
	}
	else if (cmd == "get_logs")
	{
		CommandGetLogs(rParams, rResult);
	}
	else if (cmd == "set_log_level")
	{
		CommandSetLogLevel(rParams, rResult);
	}
	else
	{
		return false;
	}
	return true;
}

} // namespace engine

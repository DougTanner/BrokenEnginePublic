#include "Agent/AgentCommands.h"

#if defined(BT_CLIENT)

#include "Graphics/Screenshot.h"

#include "Agent/AgentScene.h"
#include "Game.h"

namespace game
{

namespace
{

std::filesystem::path PathFromParam(const nlohmann::json& rValue)
{
	// Agent-supplied path is UTF-8 (trust boundary); .get<std::string>() throws on a non-string.
	std::string utf8 = rValue.get<std::string>();
	return std::filesystem::path(reinterpret_cast<const char8_t*>(utf8.c_str()));
}

// screenshot: capture the live window to a downscaled JPG/PNG. Completes via the deferred-response mechanism once
// the async save records its result. Schema: {"path"?,"maxWidth"?:1568,"format"?:"jpg|png","quality"?:80}.
void CommandScreenshot(const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	// The capture consume site (RenderMainPresentAcquire) is compiled out under !kbScreenshots, so a deferred request
	// would never resolve — fail fast instead of blocking the agent channel until the liveness timeout.
	if constexpr (!kbScreenshots)
	{
		throw std::runtime_error("screenshot capture is compiled out (kbScreenshots is false)");
	}

	engine::ScreenshotRequest request;
	request.bPublishResult = true;
	if (rParams.contains("path"))
	{
		request.path = PathFromParam(rParams.at("path"));
	}
	if (rParams.contains("maxWidth"))
	{
		request.iMaxWidth = rParams.at("maxWidth").get<int64_t>();
	}
	if (rParams.contains("format"))
	{
		std::string format = rParams.at("format").get<std::string>();
		if (format == "png")
		{
			request.bPng = true;
		}
		else if (format == "jpg" || format == "jpeg")
		{
			request.bPng = false;
		}
		else
		{
			throw std::runtime_error("screenshot 'format' must be 'jpg' or 'png'");
		}
	}
	if (rParams.contains("quality"))
	{
		request.iQuality = std::clamp<int64_t>(rParams.at("quality").get<int64_t>(), 1, 100);
	}

	uint64_t uiCaptureToken = engine::ResetCaptureResult();
	request.uiCaptureToken = uiCaptureToken;
	engine::gpGraphics->mScreenshotRequest = std::move(request);
	engine::gpAgentCommandServer->DeferResponse([uiCaptureToken]()
	{
		std::optional<nlohmann::json> result = engine::TakeCaptureResult(uiCaptureToken);
		// A failed async save publishes an {"error":...} result; rethrow it so Drain's poll-exception path emits a
		// proper {"ok":false,"error"} envelope instead of wrapping the error JSON as ok:true.
		if (result.has_value() && result->contains("error"))
		{
			throw std::runtime_error(result->at("error").get<std::string>());
		}
		return result;
	});
}

// dump_render_target: read back an offscreen render target and encode it (normalized grayscale PNG for
// single-channel, direct PNG for 4x8-bit, optional raw .bin). Schema: {"name","index"?:0,"channel"?:0,"path"?,"raw"?:false}.
void CommandDumpRenderTarget(const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	// As CommandScreenshot: the readback consume site is compiled out under !kbScreenshots, so fail fast.
	if constexpr (!kbScreenshots)
	{
		throw std::runtime_error("dump_render_target is compiled out (kbScreenshots is false)");
	}

	if (!rParams.contains("name") || !rParams.at("name").is_string())
	{
		throw std::runtime_error("dump_render_target requires string 'name'");
	}

	engine::DumpRenderTargetRequest request;
	request.bPublishResult = true;
	request.name = rParams.at("name").get<std::string>();
	if (rParams.contains("index"))
	{
		request.iIndex = rParams.at("index").get<int64_t>();
	}
	if (rParams.contains("channel"))
	{
		request.iChannel = rParams.at("channel").get<int64_t>();
	}
	if (rParams.contains("path"))
	{
		request.path = PathFromParam(rParams.at("path"));
	}
	if (rParams.contains("raw"))
	{
		request.bRaw = rParams.at("raw").get<bool>();
	}

	// Trust-boundary validation now (unknown name / bad index / non-encodable format) so errors report synchronously.
	engine::ValidateDumpRenderTargetRequest(request);

	uint64_t uiCaptureToken = engine::ResetCaptureResult();
	request.uiCaptureToken = uiCaptureToken;
	engine::gpGraphics->mDumpRenderTargetRequest = std::move(request);
	engine::gpAgentCommandServer->DeferResponse([uiCaptureToken]()
	{
		std::optional<nlohmann::json> result = engine::TakeCaptureResult(uiCaptureToken);
		// A failed async encode publishes an {"error":...} result; rethrow it so Drain's poll-exception path emits a
		// proper {"ok":false,"error"} envelope instead of wrapping the error JSON as ok:true.
		if (result.has_value() && result->contains("error"))
		{
			throw std::runtime_error(result->at("error").get<std::string>());
		}
		return result;
	});
}

// Full structured dump of the last completed ImGui frame (registry read table) plus game UI state.
nlohmann::json BuildDescribeUi()
{
	nlohmann::json result;
	result["uiState"] = UiStateName(gpGame->meUiState);
	result["gameFlags"] = GameFlagNames(gpGame->mGameFlags);

	result["framebuffer"] = {engine::gpGraphics->mFramebufferExtent2D.width, engine::gpGraphics->mFramebufferExtent2D.height};

	ImVec2 mousePos = (ImGui::GetCurrentContext() != nullptr) ? ImGui::GetIO().MousePos : ImVec2(0.0f, 0.0f);
	result["mouse"] = {mousePos.x, mousePos.y};

	nlohmann::json windows = nlohmann::json::array();
	nlohmann::json items = nlohmann::json::array();
	if (engine::gpAgentUiRegistry != nullptr)
	{
		for (int64_t i = 0; i < engine::gpAgentUiRegistry->WindowCount(); ++i)
		{
			const engine::AgentUiWindow& rWindow = engine::gpAgentUiRegistry->Window(i);
			windows.push_back({{"name", rWindow.pcName}, {"rect", {rWindow.f4Rect.x, rWindow.f4Rect.y, rWindow.f4Rect.z, rWindow.f4Rect.w}}, {"focused", rWindow.bFocused}});
		}
		for (int64_t i = 0; i < engine::gpAgentUiRegistry->ItemCount(); ++i)
		{
			const engine::AgentUiItem& rItem = engine::gpAgentUiRegistry->Item(i);
			if (rItem.pcLabel[0] == '\0')
			{
				continue; // no label recorded (invisible / no ItemInfo) — not addressable, omit
			}
			items.push_back(
			{
				{"label", rItem.pcLabel},
				{"window", rItem.pcWindow},
				{"rect", {rItem.f4Rect.x, rItem.f4Rect.y, rItem.f4Rect.z, rItem.f4Rect.w}},
				{"disabled", rItem.bDisabled},
				{"checked", (rItem.iStatusFlags & ImGuiItemStatusFlags_Checked) != 0},
				{"inputable", (rItem.iStatusFlags & ImGuiItemStatusFlags_Inputable) != 0},
				{"hovered", (rItem.iStatusFlags & ImGuiItemStatusFlags_HoveredRect) != 0},
			});
		}
	}
	result["windows"] = std::move(windows);
	result["items"] = std::move(items);
	return result;
}

// Comma-joined list of candidate labels (optionally filtered to one window) for a not-found / ambiguous error.
std::string CandidateLabels(const char* pcWindow)
{
	std::string candidates;
	if (engine::gpAgentUiRegistry == nullptr)
	{
		return candidates;
	}
	for (int64_t i = 0; i < engine::gpAgentUiRegistry->ItemCount(); ++i)
	{
		const engine::AgentUiItem& rItem = engine::gpAgentUiRegistry->Item(i);
		if (rItem.pcLabel[0] == '\0')
		{
			continue;
		}
		if (pcWindow != nullptr && std::strcmp(rItem.pcWindow, pcWindow) != 0)
		{
			continue;
		}
		if (!candidates.empty())
		{
			candidates += ", ";
		}
		candidates += rItem.pcLabel;
		if (candidates.size() > 1024) // bound the error message
		{
			candidates += ", ...";
			break;
		}
	}
	return candidates;
}

// Win32 VK code for a named key (letters/digits directly; a small symbolic table for the game bindings).
int32_t ParseKeyVk(const std::string& rName)
{
	if (rName.size() == 1)
	{
		char cChar = rName[0];
		if (cChar >= 'a' && cChar <= 'z')
		{
			cChar = static_cast<char>(cChar - 'a' + 'A');
		}
		if ((cChar >= 'A' && cChar <= 'Z') || (cChar >= '0' && cChar <= '9'))
		{
			return static_cast<int32_t>(static_cast<unsigned char>(cChar));
		}
	}
	if (rName == "ESC" || rName == "ESCAPE")
	{
		return VK_ESCAPE;
	}
	if (rName == "SPACE")
	{
		return VK_SPACE;
	}
	if (rName == "TAB")
	{
		return VK_TAB;
	}
	if (rName == "ENTER" || rName == "RETURN")
	{
		return VK_RETURN;
	}
	if (rName == "UP")
	{
		return VK_UP;
	}
	if (rName == "DOWN")
	{
		return VK_DOWN;
	}
	if (rName == "LEFT")
	{
		return VK_LEFT;
	}
	if (rName == "RIGHT")
	{
		return VK_RIGHT;
	}
	if (rName.size() >= 2 && (rName[0] == 'F' || rName[0] == 'f'))
	{
		int32_t iNumber = std::atoi(rName.c_str() + 1);
		if (iNumber >= 1 && iNumber <= 24)
		{
			return VK_F1 + (iNumber - 1);
		}
	}
	throw std::runtime_error("unknown key");
}

// Fill a bounded char buffer from a string param (trust boundary — .get<std::string>() throws on non-string).
void CopyStringParam(char* pcDst, int64_t iDstSize, const std::string& rSource)
{
	int64_t i = 0;
	for (; i < iDstSize - 1 && i < static_cast<int64_t>(rSource.size()); ++i)
	{
		pcDst[i] = rSource[i];
	}
	pcDst[i] = '\0';
}

// Shared label-target parse for click / hover / set_slider.
void FillLabelTarget(const nlohmann::json& rParams, engine::AgentScript& rScript)
{
	if (!rParams.contains("label") || !rParams.at("label").is_string())
	{
		throw std::runtime_error("command requires string 'label'");
	}
	CopyStringParam(rScript.pcLabel, static_cast<int64_t>(sizeof(rScript.pcLabel)), rParams.at("label").get<std::string>());
	if (rParams.contains("window"))
	{
		CopyStringParam(rScript.pcWindow, static_cast<int64_t>(sizeof(rScript.pcWindow)), rParams.at("window").get<std::string>());
		rScript.bHasWindow = true;
	}
}

// Begin a script (throwing "busy" if one is already running) and defer the response until the script completes.
// For label-based scripts pcErrorWindow (may be null) drives the candidate list on a not-found / ambiguous error.
void BeginScriptAndDefer(const engine::AgentScript& rScript, bool bDescribeUiAfter, bool bLabelBased, bool bHasWindow, std::string errorWindow)
{
	if (!engine::gpAgentInput->BeginScript(rScript))
	{
		throw std::runtime_error("busy");
	}

	engine::gpAgentCommandServer->DeferResponse([bDescribeUiAfter, bLabelBased, bHasWindow, errorWindow]() -> std::optional<nlohmann::json>
	{
		engine::AgentScriptStatus eStatus = engine::gpAgentInput->ScriptStatus();
		if (eStatus == engine::AgentScriptStatus::kPending)
		{
			return std::nullopt;
		}
		if (eStatus == engine::AgentScriptStatus::kTimeout)
		{
			throw std::runtime_error("timed out");
		}
		if (eStatus == engine::AgentScriptStatus::kNotFound)
		{
			throw std::runtime_error("no widget matches label; candidates: " + CandidateLabels(bHasWindow ? errorWindow.c_str() : nullptr));
		}
		if (eStatus == engine::AgentScriptStatus::kAmbiguous)
		{
			throw std::runtime_error("ambiguous label; candidates: " + CandidateLabels(bHasWindow ? errorWindow.c_str() : nullptr));
		}
		if (eStatus == engine::AgentScriptStatus::kNotInputable)
		{
			throw std::runtime_error("target is not inputable");
		}

		nlohmann::json result;
		if (bLabelBased)
		{
			result["found"] = true;
			result["enabled"] = !engine::gpAgentInput->ResolvedDisabled();
		}
		else
		{
			result["ok"] = true;
		}
		if (bDescribeUiAfter)
		{
			result["ui"] = BuildDescribeUi();
		}
		return result;
	});
}

void CommandDescribeUi([[maybe_unused]] const nlohmann::json& rParams, nlohmann::json& rResult)
{
	rResult = BuildDescribeUi();
}

// click {label, window?, timeoutFrames?=120, describeUiAfter?=true}: stabilize target rect, press/release the left
// mouse button at its center through ImGui IO, then optionally dump the post-click UI.
void CommandClick(const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	engine::AgentScript script;
	script.eKind = engine::AgentScriptKind::kClick;
	FillLabelTarget(rParams, script);
	if (rParams.contains("timeoutFrames"))
	{
		script.iTimeoutFrames = std::max<int32_t>(0, static_cast<int32_t>(rParams.at("timeoutFrames").get<int64_t>()));
	}
	bool bDescribeUiAfter = !rParams.contains("describeUiAfter") || rParams.at("describeUiAfter").get<bool>();
	BeginScriptAndDefer(script, bDescribeUiAfter, true, script.bHasWindow, script.pcWindow);
}

// hover {label, window?, holdFrames?=2}: move to and stabilize on the target, hold, then dump the UI.
void CommandHover(const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	engine::AgentScript script;
	script.eKind = engine::AgentScriptKind::kHover;
	FillLabelTarget(rParams, script);
	if (rParams.contains("holdFrames"))
	{
		script.iHoldFrames = std::max<int32_t>(0, static_cast<int32_t>(rParams.at("holdFrames").get<int64_t>()));
	}
	BeginScriptAndDefer(script, true, true, script.bHasWindow, script.pcWindow);
}

// set_slider {label, window?, value}: Ctrl+Click the slider to open ImGui temp-input, type the value, commit (Enter).
void CommandSetSlider(const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	if (!rParams.contains("value"))
	{
		throw std::runtime_error("set_slider requires 'value'");
	}
	engine::AgentScript script;
	script.eKind = engine::AgentScriptKind::kSetSlider;
	FillLabelTarget(rParams, script);
	double dValue = rParams.at("value").get<double>();
	std::snprintf(script.pcValueText, sizeof(script.pcValueText), "%g", dValue);
	BeginScriptAndDefer(script, false, true, script.bHasWindow, script.pcWindow);
}

// key {key, holdFrames?=1}: hold then release a named VK through the RawInput overlay, driving KeyboardPressed edges.
void CommandKey(const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	if (!rParams.contains("key") || !rParams.at("key").is_string())
	{
		throw std::runtime_error("key requires string 'key'");
	}
	engine::AgentScript script;
	script.eKind = engine::AgentScriptKind::kKey;
	script.iKeyVk = ParseKeyVk(rParams.at("key").get<std::string>());
	script.iHoldFrames = rParams.contains("holdFrames") ? std::max<int32_t>(0, static_cast<int32_t>(rParams.at("holdFrames").get<int64_t>())) : 1;
	BeginScriptAndDefer(script, false, false, false, std::string());
}

// mouse {x, y, action:"move|down|up|click|wheel", button?="left", notches?}: raw pixel coords feeding both the ImGui
// IO sink and the RawInput overlay (normalized) for world clicks / unlabeled targets and camera-zoom wheel.
void CommandMouse(const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	engine::AgentScript script;
	script.eKind = engine::AgentScriptKind::kMouse;

	std::string action = rParams.contains("action") ? rParams.at("action").get<std::string>() : "move";
	if (action == "move")
	{
		script.eMouseAction = engine::AgentMouseAction::kMove;
	}
	else if (action == "down")
	{
		script.eMouseAction = engine::AgentMouseAction::kDown;
	}
	else if (action == "up")
	{
		script.eMouseAction = engine::AgentMouseAction::kUp;
	}
	else if (action == "click")
	{
		script.eMouseAction = engine::AgentMouseAction::kClick;
	}
	else if (action == "wheel")
	{
		script.eMouseAction = engine::AgentMouseAction::kWheel;
	}
	else
	{
		throw std::runtime_error("mouse 'action' must be move|down|up|click|wheel");
	}

	if (script.eMouseAction == engine::AgentMouseAction::kWheel)
	{
		script.iWheelNotches = rParams.contains("notches") ? static_cast<int32_t>(rParams.at("notches").get<int64_t>()) : 1;
	}
	else
	{
		if (!rParams.contains("x") || !rParams.contains("y"))
		{
			throw std::runtime_error("mouse requires 'x' and 'y'");
		}
		script.f2CoordPixels[0] = static_cast<float>(rParams.at("x").get<double>());
		script.f2CoordPixels[1] = static_cast<float>(rParams.at("y").get<double>());
		script.bHasCoord = true;

		std::string button = rParams.contains("button") ? rParams.at("button").get<std::string>() : "left";
		if (button == "left")
		{
			script.iImGuiMouseButton = 0;
			script.uiOverlayMouseButtonBit = engine::kMouseButtonLeft;
		}
		else if (button == "right")
		{
			script.iImGuiMouseButton = 1;
			script.uiOverlayMouseButtonBit = engine::kMouseButtonRight;
		}
		else if (button == "middle")
		{
			script.iImGuiMouseButton = 2;
			script.uiOverlayMouseButtonBit = engine::kMouseButtonMiddle;
		}
		else
		{
			throw std::runtime_error("mouse 'button' must be left|right|middle");
		}
	}

	BeginScriptAndDefer(script, false, false, false, std::string());
}

} // namespace

bool ExecuteAgentCommandClient(std::string_view cmd, const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if (cmd == "screenshot")
	{
		CommandScreenshot(rParams, rResult);
		return true;
	}
	if (cmd == "dump_render_target")
	{
		CommandDumpRenderTarget(rParams, rResult);
		return true;
	}
	if (cmd == "describe_ui")
	{
		CommandDescribeUi(rParams, rResult);
		return true;
	}
	if (cmd == "click")
	{
		CommandClick(rParams, rResult);
		return true;
	}
	if (cmd == "hover")
	{
		CommandHover(rParams, rResult);
		return true;
	}
	if (cmd == "set_slider")
	{
		CommandSetSlider(rParams, rResult);
		return true;
	}
	if (cmd == "key")
	{
		CommandKey(rParams, rResult);
		return true;
	}
	if (cmd == "mouse")
	{
		CommandMouse(rParams, rResult);
		return true;
	}
	if (cmd == "describe_scene")
	{
		CommandDescribeScene(rParams, rResult);
		return true;
	}
	return false;
}

} // namespace game

#endif // defined(BT_CLIENT)

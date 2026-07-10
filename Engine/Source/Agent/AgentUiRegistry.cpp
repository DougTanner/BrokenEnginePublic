#include "Pch.h"

#if defined(BT_CLIENT)

#include "Agent/AgentUiRegistry.h"

namespace engine
{

namespace
{

// Bounded copy into a fixed char buffer, always null-terminated. pcSrc may be null.
void CopyTruncate(char* pcDst, int64_t iDstSize, const char* pcSrc)
{
	if (pcSrc == nullptr)
	{
		pcDst[0] = '\0';
		return;
	}
	int64_t i = 0;
	for (; i < iDstSize - 1 && pcSrc[i] != '\0'; ++i)
	{
		pcDst[i] = pcSrc[i];
	}
	pcDst[i] = '\0';
}

// Length of the display portion of an ImGui label — everything before the "##" id separator (or the whole string).
int64_t DisplayLength(const char* pcLabel)
{
	for (int64_t i = 0; pcLabel[i] != '\0'; ++i)
	{
		if (pcLabel[i] == '#' && pcLabel[i + 1] == '#')
		{
			return i;
		}
	}
	int64_t iLength = 0;
	while (pcLabel[iLength] != '\0')
	{
		++iLength;
	}
	return iLength;
}

char LowerAscii(char cChar)
{
	return (cChar >= 'A' && cChar <= 'Z') ? static_cast<char>(cChar - 'A' + 'a') : cChar;
}

// Case-insensitive substring test: is pcNeedle contained in pcHaystack?
bool ContainsCaseInsensitive(const char* pcHaystack, const char* pcNeedle)
{
	if (pcNeedle[0] == '\0')
	{
		return true;
	}
	for (int64_t i = 0; pcHaystack[i] != '\0'; ++i)
	{
		int64_t j = 0;
		while (pcNeedle[j] != '\0' && LowerAscii(pcHaystack[i + j]) == LowerAscii(pcNeedle[j]))
		{
			++j;
		}
		if (pcNeedle[j] == '\0')
		{
			return true;
		}
	}
	return false;
}

} // namespace

AgentUiRegistry::AgentUiRegistry()
{
	ASSERT(gpAgentUiRegistry == nullptr);
	gpAgentUiRegistry = this;
}

AgentUiRegistry::~AgentUiRegistry()
{
	if (gpAgentUiRegistry == this)
	{
		gpAgentUiRegistry = nullptr;
	}
}

void AgentUiRegistry::HookItemAdd(ImGuiID uiId, const XMFLOAT4& rf4Rect, const char* pcWindow, bool bDisabled)
{
	int64_t& riCount = miItemCount[miWrite];
	if (riCount >= kiMaxItems)
	{
		return; // capacity bound — silently drop overflow (registry is best-effort snapshot)
	}
	AgentUiItem& rItem = mItems[miWrite][riCount];
	rItem.uiId = uiId;
	rItem.f4Rect = rf4Rect;
	rItem.iStatusFlags = 0;
	rItem.bDisabled = bDisabled;
	rItem.pcLabel[0] = '\0';
	CopyTruncate(rItem.pcWindow, sizeof(rItem.pcWindow), pcWindow);
	++riCount;
}

void AgentUiRegistry::HookItemInfo(ImGuiID uiId, const char* pcLabel, int32_t iStatusFlags)
{
	// ItemInfo fires immediately after ItemAdd for the same id — search backward from the tail.
	int64_t iCount = miItemCount[miWrite];
	for (int64_t i = iCount - 1; i >= 0; --i)
	{
		AgentUiItem& rItem = mItems[miWrite][i];
		if (rItem.uiId == uiId)
		{
			if (pcLabel != nullptr && pcLabel[0] != '\0')
			{
				CopyTruncate(rItem.pcLabel, sizeof(rItem.pcLabel), pcLabel);
			}
			rItem.iStatusFlags |= iStatusFlags;
			return;
		}
	}
}

void AgentUiRegistry::Swap()
{
	// Snapshot the window list of the just-completed frame into the write buffer before flipping.
	ImGuiContext* pContext = ImGui::GetCurrentContext();
	int64_t& riWindowCount = miWindowCount[miWrite];
	riWindowCount = 0;
	if (pContext != nullptr)
	{
		for (ImGuiWindow* pWindow : pContext->Windows)
		{
			if (pWindow == nullptr || !pWindow->WasActive)
			{
				continue;
			}
			if (riWindowCount >= kiMaxWindows)
			{
				break;
			}
			AgentUiWindow& rWindow = mWindows[miWrite][riWindowCount];
			CopyTruncate(rWindow.pcName, sizeof(rWindow.pcName), pWindow->Name);
			rWindow.f4Rect = XMFLOAT4(pWindow->Pos.x, pWindow->Pos.y, pWindow->Pos.x + pWindow->Size.x, pWindow->Pos.y + pWindow->Size.y);
			rWindow.bFocused = (pContext->NavWindow == pWindow);
			++riWindowCount;
		}
	}

	// Publish: the write buffer becomes the read buffer; reset the new write buffer's item count for the next frame.
	std::swap(miWrite, miRead);
	miItemCount[miWrite] = 0;
}

int64_t AgentUiRegistry::ResolveLabel(const char* pcLabel, const char* pcWindow) const
{
	int64_t iCount = miItemCount[miRead];
	int64_t iNeedleDisplay = DisplayLength(pcLabel);

	// Three resolution tiers, tried in order; the first tier with any match decides (single -> hit, multiple -> ambiguous).
	for (int64_t iTier = 0; iTier < 3; ++iTier)
	{
		int64_t iFound = kNotFound;
		int64_t iMatches = 0;
		for (int64_t i = 0; i < iCount; ++i)
		{
			const AgentUiItem& rItem = mItems[miRead][i];
			if (pcWindow != nullptr && std::strcmp(rItem.pcWindow, pcWindow) != 0)
			{
				continue;
			}
			if (rItem.pcLabel[0] == '\0')
			{
				continue;
			}

			bool bMatch = false;
			if (iTier == 0)
			{
				bMatch = std::strcmp(rItem.pcLabel, pcLabel) == 0;
			}
			else if (iTier == 1)
			{
				int64_t iItemDisplay = DisplayLength(rItem.pcLabel);
				bMatch = iItemDisplay == iNeedleDisplay && std::strncmp(rItem.pcLabel, pcLabel, static_cast<size_t>(iNeedleDisplay)) == 0;
			}
			else
			{
				bMatch = ContainsCaseInsensitive(rItem.pcLabel, pcLabel);
			}

			if (bMatch)
			{
				++iMatches;
				iFound = i;
			}
		}

		if (iMatches == 1)
		{
			return iFound;
		}
		if (iMatches > 1)
		{
			return kAmbiguous;
		}
	}

	return kNotFound;
}

} // namespace engine

// ---------------------------------------------------------------------------------------------------------------------
// Dear ImGui test-engine hook externs (imgui_internal.h:3951-3954). The vendored imgui is compiled with IMGUI_ENABLE_TEST_ENGINE
// (ThirdParty/Prebuilts/Source/Engine/ImGui.cpp), so these are invoked from ItemAdd()/ItemInfo() when
// g.TestEngineHookItems is set (ImGuiManager ctor, agent layer active). They forward into the client-only registry.
// Global scope, matching the extern declarations. The server links imgui too (BT_ENGINE include) — its no-op
// implementations live in AgentUiRegistryServerStubs.cpp.

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, [[maybe_unused]] const ImGuiLastItemData* item_data)
{
	if (engine::gpAgentUiRegistry == nullptr)
	{
		return;
	}
	const char* pcWindow = (ctx->CurrentWindow != nullptr) ? ctx->CurrentWindow->Name : "";
	bool bDisabled = (ctx->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
	engine::gpAgentUiRegistry->HookItemAdd(id, XMFLOAT4(bb.Min.x, bb.Min.y, bb.Max.x, bb.Max.y), pcWindow, bDisabled);
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags)
{
	if (engine::gpAgentUiRegistry == nullptr)
	{
		return;
	}
	// Begin() self-registers each window (ITEM_ADD + ITEM_INFO with the window name, imgui.cpp:7764-7765) with flags that
	// never include ImGuiItemStatusFlags_Visible (only widget ItemAdd sets it) — recording that label would make on-screen
	// windows read visible:false in describe_ui and window-name clicks fail kClipped; window names stay queryable via the
	// windows snapshot. The window pseudo-item keeps an empty label, so it is filtered from describe_ui items and unresolvable
	// by label (kNotFound) — intended.
	if (ctx->CurrentWindow != nullptr && id == ctx->CurrentWindow->ID)
	{
		return;
	}
	engine::gpAgentUiRegistry->HookItemInfo(id, label, static_cast<int32_t>(flags));
}

void ImGuiTestEngineHook_Log([[maybe_unused]] ImGuiContext* ctx, [[maybe_unused]] const char* fmt, ...)
{
	// No-op: the registry needs item rects/labels only, not the test-engine log stream.
}

const char* ImGuiTestEngine_FindItemDebugLabel([[maybe_unused]] ImGuiContext* ctx, [[maybe_unused]] ImGuiID id)
{
	return nullptr;
}

#endif // defined(BT_CLIENT)

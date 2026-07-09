#pragma once

#if defined(BT_CLIENT)

namespace engine
{

// One completed-frame ImGui widget record. Filled by the imgui test-engine hooks (ImGuiTestEngineHook_ItemAdd /
// _ItemInfo) during ImGui::NewFrame..Render into the write table, made readable after ImGui::Render() by Swap().
struct AgentUiItem
{
	ImGuiID uiId = 0;
	char pcLabel[64] {};
	char pcWindow[32] {};
	XMFLOAT4 f4Rect {}; // pixels: {minX, minY, maxX, maxY}
	int32_t iStatusFlags = 0; // ImGuiItemStatusFlags_ bits merged from ItemInfo
	bool bDisabled = false;
};

// One completed-frame ImGui window record (name + pixel rect + focus), snapshotted from g.Windows at Swap().
struct AgentUiWindow
{
	char pcName[32] {};
	XMFLOAT4 f4Rect {}; // pixels: {minX, minY, maxX, maxY}
	bool bFocused = false;
};

// Double-buffered fixed-capacity snapshot of every ImGui widget/window per completed frame. The four imgui
// test-engine hook externs (implemented in AgentUiRegistry.cpp) fill the write table each frame; Swap() (after
// ImGui::Render()) publishes it as the read table, so readers (describe_ui, label resolution) always see the last
// COMPLETED frame. Zero steady-state heap: the tables are member arrays sized once at construction.
class AgentUiRegistry
{
public:

	AgentUiRegistry();
	~AgentUiRegistry();

	AgentUiRegistry(const AgentUiRegistry&) = delete;
	AgentUiRegistry& operator=(const AgentUiRegistry&) = delete;

	// Write-table fill (called from the imgui hooks during ImGui::NewFrame..Render).
	void HookItemAdd(ImGuiID uiId, const XMFLOAT4& rf4Rect, const char* pcWindow, bool bDisabled);
	void HookItemInfo(ImGuiID uiId, const char* pcLabel, int32_t iStatusFlags);

	// Publish the write table as the read table and snapshot the window list. Call after ImGui::Render().
	void Swap();

	// Read-table accessors (last completed frame).
	int64_t ItemCount() const { return miItemCount[miRead]; }
	const AgentUiItem& Item(int64_t iIndex) const { return mItems[miRead][iIndex]; }
	int64_t WindowCount() const { return miWindowCount[miRead]; }
	const AgentUiWindow& Window(int64_t iIndex) const { return mWindows[miRead][iIndex]; }

	// Resolve a label to a read-table item index. Order: exact full label -> exact pre-"##" display portion ->
	// case-insensitive substring. Returns the index, kNotFound (no match), or kAmbiguous (>1 at the deciding tier).
	// pcWindow, when non-null, restricts matching to items in that window.
	static constexpr int64_t kNotFound = -1;
	static constexpr int64_t kAmbiguous = -2;
	int64_t ResolveLabel(const char* pcLabel, const char* pcWindow) const;

	static constexpr int64_t kiMaxItems = 1024;
	static constexpr int64_t kiMaxWindows = 64;

private:

	AgentUiItem mItems[2][kiMaxItems];
	int64_t miItemCount[2] {};
	AgentUiWindow mWindows[2][kiMaxWindows];
	int64_t miWindowCount[2] {};

	int64_t miWrite = 0;
	int64_t miRead = 1;
};

inline AgentUiRegistry* gpAgentUiRegistry = nullptr;

} // namespace engine

#endif // defined(BT_CLIENT)

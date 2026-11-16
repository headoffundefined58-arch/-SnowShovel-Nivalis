// Menu integration with SKSE Menu Framework (QTR-Modding v3 API).
//
// This translation unit is compiled WITHOUT the CommonLib PCH: the menu API
// header includes <windows.h>, whose types collide with CommonLibSSE-NG's
// parallel REX::W32 implementation when the PCH is force-included. All data
// crosses MenuBridge by value; nothing in here touches CommonLib.

#include <windows.h>

#include <filesystem>
#include <utility>
#include <string>
#include <spdlog/spdlog.h>

namespace RE
{
	class InputEvent;  // only referenced as a pointer typedef by the header
}

#include "SKSEMenuFramework.h"

#include "MenuBridge.h"

namespace
{
	using namespace ImGuiMCP;

	// Bilingual label: Chinese is the default, English is one click away.
	const char* L(const char* a_zh, const char* a_en)
	{
		return SnowMenu::LanguageIsEn() ? a_en : a_zh;
	}

	// Keep in sync with SKSEPluginInfo in main.cpp.
	constexpr const char* kVersion = "0.5.1";

	// DirectInput scan codes -> display names (the common ones only).
	std::string KeyName(int a_code)
	{
		static const std::pair<int, const char*> names[] = {
			{ 1, "Esc" }, { 2, "1" }, { 3, "2" }, { 4, "3" }, { 5, "4" }, { 6, "5" }, { 7, "6" },
			{ 8, "7" }, { 9, "8" }, { 10, "9" }, { 11, "0" }, { 14, "Backspace" }, { 28, "Enter" },
			{ 16, "Q" }, { 17, "W" }, { 18, "E" }, { 19, "R" }, { 20, "T" }, { 21, "Y" },
			{ 22, "U" }, { 23, "I" }, { 24, "O" }, { 25, "P" }, { 26, "[" }, { 27, "]" },
			{ 30, "A" }, { 31, "S" }, { 32, "D" }, { 33, "F" }, { 34, "G" }, { 35, "H" },
			{ 36, "J" }, { 37, "K" }, { 38, "L" }, { 39, ";" }, { 40, "'" },
			{ 43, "\\" }, { 44, "Z" }, { 45, "X" }, { 46, "C" }, { 47, "V" }, { 48, "B" },
			{ 49, "N" }, { 50, "M" }, { 51, "," }, { 52, "." }, { 53, "/" }, { 57, "Space" },
			{ 59, "F1" }, { 60, "F2" }, { 61, "F3" }, { 62, "F4" }, { 63, "F5" }, { 64, "F6" },
			{ 65, "F7" }, { 66, "F8" }, { 67, "F9" }, { 68, "F10" },
		};
		if (a_code <= 0) {
			return L("未绑定", "unbound");
		}
		for (const auto& [code, name] : names) {
			if (code == a_code) {
				return name;
			}
		}
		return std::to_string(a_code);
	}

	const char* OnOff(bool a_value)
	{
		return a_value ? L("已就绪", "ready") : L("未启用", "off");
	}

	// Framework input hook: while one of its windows is open the framework
	// owns the keyboard, so a hotkey rebind is captured here rather than on
	// the game's own input sink. The event is only forwarded - this TU cannot
	// read RE::InputEvent, so the parsing happens on the bridge side.
	bool __stdcall OnFrameworkInput(RE::InputEvent* a_event)
	{
		return SnowMenu::HandleMenuInput(a_event);
	}

	void __stdcall RenderMenu()
	{
		auto vals = SnowMenu::Snapshot();
		auto status = SnowMenu::GetStatus();
		bool changed = false;
		static bool dirty = false;

		// Language first, so every label below follows immediately. Persisted
		// right away: it is a click, not a drag.
		{
			const char* langs[] = { "中文", "English" };
			if (Combo(L("界面语言", "Language"), &vals.language, langs, 2)) {
				SnowMenu::Apply(vals);
				SnowMenu::Persist(vals);
				status = SnowMenu::GetStatus();
			}
		}

		SeparatorText(L("运行状态", "Status"));
		Text("%s %s", L("雪相 / Nivalis", "Nivalis"), kVersion);
		Text("%s: %s", L("相变时钟(时间炼成)", "Phase clock (Alchemy of Time)"),
			status.aot ? (status.phases ? L("运行中", "running") : L("已关闭", "disabled"))
					   : L("未检测到时间炼成", "Alchemy of Time not loaded"));
		Text("%s: %s", L("动态雪面挖雪(硬需求)", "Terrain carve (hard dep)"),
			status.carve ? L("已就绪", "ready") : L("不可用", "unavailable"));
		Text("%s: %s", L("水洼(血池框架)", "Puddles (bloodpool)"),
			status.dbf ? L("已接入", "connected") : L("未加载，雪化得干", "not loaded, melt stays dry"));
		Text("%s: %d  (%s %d / %s %d / %s %d)", L("积雪记录", "Piles"),
			status.piles,
			L("雪", "snow"), status.snow,
			L("水", "water"), status.water,
			L("冰", "ice"), status.ice);

		if (CollapsingHeader(L("总开关", "General"))) {
			changed |= Checkbox(L("启用本插件", "Enable plugin"), &vals.enable);
			changed |= Checkbox(L("左上角提示", "Notifications"), &vals.notify);
			changed |= Checkbox(L("调试日志", "Debug logging"), &vals.debug);
		}

		if (CollapsingHeader(L("扫地", "Sweeping"))) {
			changed |= Checkbox(L("随时用热键扫地", "Sweep with the hotkey"), &vals.sweepAnywhere);

			// Hotkey rebinding. The capture itself runs in the gameplay input
			// sink (this translation unit cannot read input), so the button
			// only raises the request; the next key press answers it.
			{
				Text("%s: %s", L("当前热键", "Current key"), KeyName(vals.sweepKey).c_str());
				if (SnowMenu::CapturePending()) {
					TextDisabled("%s", L("请按下新的热键（Esc 取消）", "Press the new hotkey (Esc cancels)"));
					if (Button(L("取消改键", "Cancel rebind"))) {
						SnowMenu::CancelKeyCapture();
					}
				} else {
					if (Button(L("改键", "Rebind"))) {
						SnowMenu::BeginKeyCapture();
					}
					SameLine();
					if (Button(L("清除绑定", "Clear"))) {
						vals.sweepKey = 0;
						SnowMenu::Apply(vals);
						SnowMenu::Persist(vals);
					}
				}
			}

			TextDisabled("%s", L("按一下开始扫地，再按一下收起来（收工时会真正停下扫帚）。",
				"Press once to sweep, press again to put the broom away (it really stops then)."));

			changed |= SliderFloat(L("扫动间隔(秒)", "Stroke interval (s)"), &vals.sweepInterval, 0.6f, 6.0f, "%.1f");
			changed |= SliderFloat(L("开始延迟(秒)", "Start delay (s)"), &vals.sweepStartDelay, 0.0f, 3.0f, "%.1f");
			changed |= SliderFloat(L("超时保护(秒)", "Session timeout (s)"), &vals.sweepTimeout, 10.0f, 120.0f, "%.0f");
			if (Button(L("立即开始 / 停止扫地", "Toggle sweeping now"))) {
				SnowMenu::ToggleSweepNow();
			}
			TextDisabled("%s", L("扫地需要背包里有扫帚（固定规则，不在此处开关）。",
				"A broom in your pack is a fixed requirement, not a toggle."));
		}

		if (CollapsingHeader(L("相变：雪 → 水 → 冰（时间炼成）", "Phases: snow - water - ice"))) {
			changed |= Checkbox(L("启用相变", "Enable phases"), &vals.phaseEnabled);
			changed |= SliderFloat(L("融化成水所需暖时(小时)", "Warm hours to melt"), &vals.meltWarmHours, 0.5f, 24.0f, "%.1f");
			changed |= SliderFloat(L("冻结成冰所需寒时(小时)", "Cold hours to freeze"), &vals.freezeColdHours, 0.5f, 24.0f, "%.1f");
			changed |= SliderFloat(L("冰融化成水所需暖时(小时)", "Warm hours to melt ice"), &vals.iceWarmHours, 0.5f, 24.0f, "%.1f");
			changed |= SliderFloat(L("水洼存留时间(秒)", "Water soak-away (s)"), &vals.waterSoakSeconds, 10.0f, 300.0f, "%.0f");
			TextDisabled("%s", L("白天累积「暖时」、夜里累积「寒时」；谁先到谁生效。",
				"Day accrues warm hours, night accrues cold hours; whichever lands first wins."));
		}

		if (CollapsingHeader(L("热源与水洼（血池框架）", "Heat and puddles (bloodpool)"))) {
			changed |= Checkbox(L("热源融雪成水洼", "Heat melts snow into puddles"), &vals.enableMeltWater);
			changed |= SliderInt(L("每次水洼数量", "Puddles per melt"), &vals.puddleCount, 1, 8);
			changed |= SliderFloat(L("水洼散布半径", "Puddle spread"), &vals.puddleSpread, 20.0f, 400.0f, "%.0f");
			changed |= SliderFloat(L("热源判定半径", "Heat probe radius"), &vals.meltProbeRadius, 60.0f, 800.0f, "%.0f");
			changed |= SliderFloat(L("雪受热多少秒化成水", "Seconds of heat: snow"), &vals.pileMeltSeconds, 1.0f, 60.0f, "%.0f");
			changed |= SliderFloat(L("冰受热多少秒化成水", "Seconds of heat: ice"), &vals.iceMeltSeconds, 1.0f, 120.0f, "%.0f");
		}

		if (CollapsingHeader(L("动态雪面（硬需求）", "Deformable terrain (hard dep)"))) {
			changed |= Checkbox(L("扫地挖开动态雪面", "Sweeping carves the terrain"), &vals.enableTerrainCarve);
			changed |= SliderFloat(L("挖雪冷却(秒)", "Carve cooldown (s)"), &vals.carveCooldown, 0.5f, 10.0f, "%.1f");
			changed |= Checkbox(L("扫拢松雪成堆", "Gather loose snow into piles"), &vals.gatherPiles);
		}

		if (changed) {
			SnowMenu::Apply(vals);
			dirty = true;
		}

		// Sliders fire while dragging; only touch the disk once the last item
		// is released.
		if (dirty && !IsAnyItemActive()) {
			SnowMenu::Persist(vals);
			dirty = false;
		}
	}
}

namespace SnowMenu
{
	// The framework knows whether any of its windows is up; gameplay keys
	// (sweeping, rebinding) must stand down while that is true.
	bool MenuBlocking()
	{
		return SKSEMenuFramework::IsInstalled() && SKSEMenuFramework::IsAnyBlockingWindowOpened();
	}

	void InitializeMenu()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			spdlog::info("[Nivalis] SKSE Menu Framework not installed; menu stays off");
			return;
		}

		SKSEMenuFramework::SetSection("Nivalis");
		SKSEMenuFramework::AddSectionItem("雪相 / Nivalis", &RenderMenu);

		// Kept alive for the session: the framework calls it on every input
		// event while its windows are open.
		static auto* hook = SKSEMenuFramework::AddInputEvent(&OnFrameworkInput);
		(void)hook;
	}
}

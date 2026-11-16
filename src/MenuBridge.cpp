#include "PCH.h"

#include "MenuBridge.h"

#include "Action.h"
#include "Alchemy.h"
#include "Piles.h"
#include "Settings.h"
#include "Water.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{
	using namespace Snow;

	// Set by the menu, consumed by the gameplay input sink (both run on
	// different threads in practice: menu renders, the sink is called by the
	// game's input pump).
	std::atomic<bool> g_capturePending{ false };

	// Same two paths Settings.cpp reads from.
	std::filesystem::path IniPath()
	{
		const std::filesystem::path primary = "Data\\SKSE\\Plugins\\Nivalis.ini";
		const std::filesystem::path fallback = "..\\Data\\SKSE\\Plugins\\Nivalis.ini";
		if (std::filesystem::exists(primary)) {
			return primary;
		}
		if (std::filesystem::exists(fallback)) {
			return fallback;
		}
		return primary;
	}

	[[nodiscard]] std::string Lower(std::string a_value)
	{
		std::transform(a_value.begin(), a_value.end(), a_value.begin(),
			[](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
		return a_value;
	}

	[[nodiscard]] std::string Trim(std::string a_value)
	{
		const auto first = a_value.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) {
			return {};
		}
		const auto last = a_value.find_last_not_of(" \t\r\n");
		return a_value.substr(first, last - first + 1);
	}

	[[nodiscard]] std::string BoolText(bool a_value) { return a_value ? "true" : "false"; }

	[[nodiscard]] std::string FloatText(float a_value)
	{
		std::ostringstream out;
		out << a_value;
		return out.str();
	}

	// Replace the first "Key = ..." line whose key matches (case
	// insensitive); append at the end when the ini never had it.
	void WriteIniValue(const std::string& a_key, const std::string& a_value)
	{
		const auto path = IniPath();

		std::vector<std::string> lines;
		{
			std::ifstream in(path);
			if (in.is_open()) {
				std::string line;
				while (std::getline(in, line)) {
					if (!line.empty() && line.back() == '\r') {
						line.pop_back();
					}
					lines.push_back(line);
				}
			}
		}

		const std::string needle = Lower(a_key);
		bool replaced = false;
		for (auto& line : lines) {
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			if (Lower(Trim(line.substr(0, eq))) != needle) {
				continue;
			}
			line = a_key + " = " + a_value;
			replaced = true;
			break;
		}
		if (!replaced) {
			lines.push_back(a_key + " = " + a_value);
		}

		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (out.is_open()) {
			for (std::size_t i = 0; i < lines.size(); ++i) {
				out << lines[i];
				if (i + 1 < lines.size()) {
					out << "\r\n";
				}
			}
		} else {
			spdlog::warn("[Nivalis] could not write settings to {}", path.string());
		}
	}
}

namespace SnowMenu
{
	bool LanguageIsEn()
	{
		return Lower(Snow::Get().language) == "en";
	}

	Values Snapshot()
	{
		const auto& s = Snow::Get();
		Values v;
		v.language = LanguageIsEn() ? kLangEN : kLangCN;
		v.enable = s.enable;
		v.notify = s.notify;
		v.debug = s.debug;
		v.sweepAnywhere = s.sweepAnywhere;
		v.sweepKey = s.sweepKey;
		v.sweepStartDelay = s.sweepStartDelay;
		v.sweepInterval = s.sweepInterval;
		v.sweepTimeout = s.sweepTimeout;
		v.enableTerrainCarve = s.enableTerrainCarve;
		v.carveCooldown = s.carveCooldown;
		v.gatherPiles = s.gatherPiles;
		v.phaseEnabled = s.phaseEnabled;
		v.meltWarmHours = s.meltWarmHours;
		v.freezeColdHours = s.freezeColdHours;
		v.iceWarmHours = s.iceWarmHours;
		v.waterSoakSeconds = s.waterSoakSeconds;
		v.enableMeltWater = s.enableMeltWater;
		v.puddleCount = s.puddleCount;
		v.puddleSpread = s.puddleSpread;
		v.meltProbeRadius = s.meltProbeRadius;
		v.pileMeltSeconds = s.pileMeltSeconds;
		v.iceMeltSeconds = s.iceMeltSeconds;
		return v;
	}

	Status GetStatus()
	{
		const auto& s = Snow::Get();
		Status st;
		st.piles = static_cast<int>(Count());
		st.snow = static_cast<int>(CountPhase(Phase::kSnow));
		st.water = static_cast<int>(CountPhase(Phase::kWater));
		st.ice = static_cast<int>(CountPhase(Phase::kIce));
		st.aot = AotLoaded();
		st.phases = PhaseClockAvailable();
		st.dbf = DbfReady();
		st.carve = s.enableTerrainCarve && s.carveSpellForm != 0;
		return st;
	}

	void Apply(const Values& a_values)
	{
		auto& s = Snow::Get();
		s.language = a_values.language == kLangEN ? "en" : "cn";
		s.enable = a_values.enable;
		s.notify = a_values.notify;
		s.debug = a_values.debug;
		s.sweepAnywhere = a_values.sweepAnywhere;
		s.sweepKey = a_values.sweepKey;
		s.sweepStartDelay = a_values.sweepStartDelay;
		s.sweepInterval = a_values.sweepInterval;
		s.sweepTimeout = a_values.sweepTimeout;
		s.enableTerrainCarve = a_values.enableTerrainCarve;
		s.carveCooldown = a_values.carveCooldown;
		s.gatherPiles = a_values.gatherPiles;
		s.phaseEnabled = a_values.phaseEnabled;
		s.meltWarmHours = a_values.meltWarmHours;
		s.freezeColdHours = a_values.freezeColdHours;
		s.iceWarmHours = a_values.iceWarmHours;
		s.waterSoakSeconds = a_values.waterSoakSeconds;
		s.enableMeltWater = a_values.enableMeltWater;
		s.puddleCount = a_values.puddleCount;
		s.puddleSpread = a_values.puddleSpread;
		s.meltProbeRadius = a_values.meltProbeRadius;
		s.pileMeltSeconds = a_values.pileMeltSeconds;
		s.iceMeltSeconds = a_values.iceMeltSeconds;
	}

	void Persist(const Values& a_values)
	{
		WriteIniValue("Language", a_values.language == kLangEN ? "en" : "cn");
		WriteIniValue("Enable", BoolText(a_values.enable));
		WriteIniValue("Notify", BoolText(a_values.notify));
		WriteIniValue("Debug", BoolText(a_values.debug));
		WriteIniValue("SweepAnywhere", BoolText(a_values.sweepAnywhere));
		WriteIniValue("SweepKey", std::to_string(a_values.sweepKey));
		WriteIniValue("SweepStartDelay", FloatText(a_values.sweepStartDelay));
		WriteIniValue("SweepInterval", FloatText(a_values.sweepInterval));
		WriteIniValue("SweepTimeout", FloatText(a_values.sweepTimeout));
		WriteIniValue("EnableTerrainCarve", BoolText(a_values.enableTerrainCarve));
		WriteIniValue("CarveCooldown", FloatText(a_values.carveCooldown));
		WriteIniValue("GatherPiles", BoolText(a_values.gatherPiles));
		WriteIniValue("PhaseEnabled", BoolText(a_values.phaseEnabled));
		WriteIniValue("MeltWarmHours", FloatText(a_values.meltWarmHours));
		WriteIniValue("FreezeColdHours", FloatText(a_values.freezeColdHours));
		WriteIniValue("IceWarmHours", FloatText(a_values.iceWarmHours));
		WriteIniValue("WaterSoakSeconds", FloatText(a_values.waterSoakSeconds));
		WriteIniValue("EnableMeltWater", BoolText(a_values.enableMeltWater));
		WriteIniValue("PuddleCount", std::to_string(a_values.puddleCount));
		WriteIniValue("PuddleSpread", FloatText(a_values.puddleSpread));
		WriteIniValue("MeltProbeRadius", FloatText(a_values.meltProbeRadius));
		WriteIniValue("PileMeltSeconds", FloatText(a_values.pileMeltSeconds));
		WriteIniValue("IceMeltSeconds", FloatText(a_values.iceMeltSeconds));
		spdlog::info("[Nivalis] settings written to ini from menu");
	}

	void ToggleSweepNow()
	{
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([]() {
				// The menu itself is what pauses the game, so the pause guard
				// has to be skipped for this path.
				Snow::ToggleSweepAnywhere(true);
			});
		}
	}

	// ---- sweep hotkey rebinding ------------------------------------------

	void BeginKeyCapture()
	{
		g_capturePending.store(true);
		spdlog::info("[Nivalis] waiting for a key to bind as the sweep hotkey");
	}

	void CancelKeyCapture()
	{
		if (g_capturePending.exchange(false)) {
			spdlog::info("[Nivalis] hotkey capture cancelled");
		}
	}

	bool CapturePending()
	{
		return g_capturePending.load();
	}

	void AcceptCapturedKey(int a_scanCode)
	{
		g_capturePending.store(false);

		// Anything outside the DirectInput keyboard band is not a key.
		if (a_scanCode <= 0 || a_scanCode > 255) {
			return;
		}

		auto& s = Snow::Get();
		if (s.sweepKey == a_scanCode) {
			return;
		}

		s.sweepKey = a_scanCode;
		Snow::ResolveSweepIdle();
		WriteIniValue("SweepKey", std::to_string(a_scanCode));
		spdlog::info("[Nivalis] sweep hotkey rebound to scan code {}", a_scanCode);
	}

	bool HandleMenuInput(void* a_event)
	{
		if (!CapturePending()) {
			return false;
		}

		auto* event = static_cast<RE::InputEvent*>(a_event);
		const auto* button = event ? event->AsButtonEvent() : nullptr;
		if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard || !button->IsDown()) {
			return false;
		}

		const auto code = static_cast<int>(button->GetIDCode());
		if (code == 1) {
			CancelKeyCapture();  // Esc
		} else {
			AcceptCapturedKey(code);
		}
		return true;  // swallow, so the menu does not act on the same press
	}
}

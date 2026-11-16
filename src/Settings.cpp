#include "PCH.h"

#include "Settings.h"

#include "Forms.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace
{
	using namespace Snow;

	// Owned by the plugin: Data\SKSE\Plugins\Nivalis.ini. The second path
	// covers the MO2 launch case where the CWD is one level below the game.
	constexpr const char* kIniFile = "Data\\SKSE\\Plugins\\Nivalis.ini";
	constexpr const char* kIniAlt = "..\\Data\\SKSE\\Plugins\\Nivalis.ini";

	using IniMap = std::unordered_map<std::string, std::string>;

	[[nodiscard]] std::string Trim(std::string a_value)
	{
		const auto first = a_value.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) {
			return {};
		}
		const auto last = a_value.find_last_not_of(" \t\r\n");
		return a_value.substr(first, last - first + 1);
	}

	// Keys are matched case-insensitively, so a hand-edited "enable = false"
	// behaves the same as "Enable = false".
	[[nodiscard]] std::string Lower(std::string a_value)
	{
		std::transform(a_value.begin(), a_value.end(), a_value.begin(),
			[](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
		return a_value;
	}

	void ParseIni(std::ifstream& a_stream, IniMap& a_out)
	{
		std::string line;
		while (std::getline(a_stream, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}

			const std::string trimmed = Trim(line);
			if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#' || trimmed[0] == '[') {
				continue;
			}

			const auto eq = trimmed.find('=');
			if (eq == std::string::npos) {
				continue;
			}

			const std::string key = Lower(Trim(trimmed.substr(0, eq)));
			std::string value = Trim(trimmed.substr(eq + 1));

			// A ';' inside a value starts a comment, but only when preceded by
			// whitespace: mod names and paths never contain " ;" while almost
			// every hand written line ends with one.
			const auto semi = value.find(" ;");
			if (semi != std::string::npos) {
				value = Trim(value.substr(0, semi));
			}

			a_out[key] = value;
		}
	}

	std::vector<std::string> SplitList(const std::string& a_value)
	{
		std::vector<std::string> out;
		std::stringstream ss(a_value);
		std::string item;
		while (std::getline(ss, item, ',')) {
			const auto cleaned = Trim(item);
			if (!cleaned.empty()) {
				out.push_back(cleaned);
			}
		}
		return out;
	}

	// ---- phase timings (AOT style) ---------------------------------------
	// Data\SKSE\Plugins\Nivalis\phases.yml looks like an Alchemy of Time
	// preset: "Key: <number>" lines with '#' comments. Keeping the phase
	// clock in the same shape as AOT means the same numbers a player would
	// tune for AOT tune this framework too. The file is re-read with the ini.
	void LoadPhaseTimes(Settings& a_settings)
	{
		std::ifstream file(a_settings.phaseTimesFile);
		if (!file.is_open()) {
			return;
		}

		auto read = [](std::string value) {
			value = Trim(value);
			return value;
		};

		int applied = 0;
		std::string line;
		while (std::getline(file, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			const auto hash = line.find('#');
			if (hash != std::string::npos) {
				line = line.substr(0, hash);
			}
			const auto colon = line.find(':');
			if (colon == std::string::npos) {
				continue;
			}
			const auto key = Lower(Trim(line.substr(0, colon)));
			const auto raw = read(line.substr(colon + 1));
			if (key.empty() || raw.empty()) {
				continue;
			}
			float value = 0.0f;
			try {
				value = std::stof(raw);
			} catch (...) {
				continue;
			}
			if (value <= 0.0f) {
				continue;
			}

			if (key == "meltwarmhours") { a_settings.meltWarmHours = value; ++applied; }
			else if (key == "freezecoldhours") { a_settings.freezeColdHours = value; ++applied; }
			else if (key == "icewarmhours") { a_settings.iceWarmHours = value; ++applied; }
			else if (key == "watersoakseconds") { a_settings.waterSoakSeconds = value; ++applied; }
			else if (key == "puddleevery") { a_settings.puddleEvery = value; ++applied; }
			else if (key == "puddlespread") { a_settings.puddleSpread = value; ++applied; }
		}

		if (applied > 0) {
			spdlog::info("[Nivalis] phase timings loaded from {} ({} values)", a_settings.phaseTimesFile, applied);
		}
	}

	class Reader
	{
	public:
		explicit Reader(const IniMap& a_map) :
			_map(a_map)
		{}

		void Bool(std::string_view a_key, bool& a_out) const
		{
			const auto it = _map.find(Lower(std::string(a_key)));
			if (it == _map.end()) {
				return;
			}
			const auto v = Lower(it->second);
			a_out = v == "1" || v == "true" || v == "yes" || v == "on";
		}

		void Int(std::string_view a_key, std::int32_t& a_out) const
		{
			const auto it = _map.find(Lower(std::string(a_key)));
			if (it == _map.end()) {
				return;
			}
			try {
				a_out = std::stoi(it->second);
			} catch (...) {
			}
		}

		void Float(std::string_view a_key, float& a_out) const
		{
			const auto it = _map.find(Lower(std::string(a_key)));
			if (it == _map.end()) {
				return;
			}
			try {
				a_out = std::stof(it->second);
			} catch (...) {
			}
		}

		void String(std::string_view a_key, std::string& a_out) const
		{
			const auto it = _map.find(Lower(std::string(a_key)));
			if (it == _map.end() || it->second.empty()) {
				return;
			}
			a_out = it->second;
		}

		// Returns false when the key is absent, so callers can keep defaults.
		bool List(std::string_view a_key, std::vector<std::string>& a_out) const
		{
			const auto it = _map.find(Lower(std::string(a_key)));
			if (it == _map.end() || it->second.empty()) {
				return false;
			}
			a_out = SplitList(it->second);
			return true;
		}

	private:
		const IniMap& _map;
	};
}

namespace Snow
{
	Settings& Get()
	{
		static Settings settings;
		return settings;
	}

	void LoadSettingsImpl();

	void LoadSettings()
	{
		// Runs from SKSEPlugin_Load and from menu reloads; a throw escaping
		// either one takes the game down, so degrade to defaults instead.
		try {
			LoadSettingsImpl();
		} catch (const std::exception& e) {
			spdlog::error("[Nivalis] settings load failed ({}); using defaults", e.what());
		} catch (...) {
			spdlog::error("[Nivalis] settings load failed; using defaults");
		}
	}

	void LoadSettingsImpl()
	{
		auto& s = Get();

		// Defaults live in the header; anything the INI omits keeps them.
		s.brooms = { "Skyrim.esm:06717F" };  // Broom01
		s.broomNameKeys = { "broom", "扫帚", "扫把" };
		s.snowTextureKeys = { "snow", "snow01", "snow02", "snow03", "drift" };
		s.snowMaterialKeys = { "snow" };
		// Small -> large. Same index pairs snow with its frozen counterpart.
		s.snowStatics = {
			"Skyrim.esm:07F948",  // SnowDriftSm01
			"Skyrim.esm:07F949",  // SnowDriftSm02
			"Skyrim.esm:06021F",  // SnowDriftM01
			"Skyrim.esm:060220",  // SnowDriftM02
			"Skyrim.esm:06021D",  // SnowDriftL01
		};
		s.iceStatics = {
			"Skyrim.esm:0F1FEE",  // IcePileS01
			"Skyrim.esm:0F1FEF",  // IcePileS02
			"Skyrim.esm:0CC75E",  // IcePileM02
			"Skyrim.esm:0CC761",  // IcePileL02
			"Skyrim.esm:0CC760",  // IcePileL03
		};
		s.dumpSounds = { "Skyrim.esm:0FB9AB" };  // NPCHumanShovelDump
		s.broomSounds = { "Skyrim.esm:0FB9A4" };  // NPCHumanShovel
		// Mirrors NMN_DeformableTerrain's HeatKeywords, so both mods agree on
		// what counts as hot.
		s.heatKeywords = { "fire", "campfire", "brazier", "firepit", "candle", "torch", "forge", "smelt", "hearth" };

		std::ifstream file(kIniFile);
		if (!file.is_open()) {
			file.open(kIniAlt);
		}
		if (!file.is_open()) {
			spdlog::info("[Nivalis] no INI found; using built-in defaults");
		} else {
			IniMap map;
			ParseIni(file, map);
			const Reader read(map);

			read.Bool("Enable", s.enable);
			read.Bool("Debug", s.debug);
			read.Bool("Notify", s.notify);
			read.Int("MaxPiles", s.maxPiles);
			read.String("Language", s.language);
			s.language = Lower(s.language);
			if (s.language != "en") {
				s.language = "cn";
			}

			read.Bool("SweepAnywhere", s.sweepAnywhere);
			read.Int("SweepKey", s.sweepKey);
			read.Bool("SweepNeedBroom", s.sweepNeedBroom);
			read.Bool("TriggerOnAttack", s.triggerOnAttack);
			read.String("SweepIdle", s.sweepIdle);

			read.Float("SweepStartDelay", s.sweepStartDelay);
			read.Float("SweepInterval", s.sweepInterval);
			read.Float("SweepTimeout", s.sweepTimeout);
			read.String("SweepStopEvent", s.sweepStopEvent);

			read.Bool("EnableTerrainCarve", s.enableTerrainCarve);
			read.String("CarveSpell", s.carveSpell);
			read.Float("CarveCooldown", s.carveCooldown);
			read.Bool("GatherPiles", s.gatherPiles);
			read.Float("GatherRadius", s.gatherRadius);
			read.Float("ForwardDistance", s.forwardDistance);

			read.Bool("PhaseEnabled", s.phaseEnabled);
			read.Bool("PhaseRequireAOT", s.phaseRequireAot);
			read.Float("MeltWarmHours", s.meltWarmHours);
			read.Float("FreezeColdHours", s.freezeColdHours);
			read.Float("IceWarmHours", s.iceWarmHours);
			read.Float("WaterSoakSeconds", s.waterSoakSeconds);
			read.Float("PuddleEvery", s.puddleEvery);
			read.Int("PuddleCount", s.puddleCount);
			read.Float("PuddleSpread", s.puddleSpread);
			read.String("PuddleProfileID", s.puddleProfileID);

			read.Bool("EnableMeltWater", s.enableMeltWater);
			read.Float("MeltWaterInterval", s.meltWaterInterval);
			read.Float("MeltProbeRadius", s.meltProbeRadius);
			read.Float("PileMeltSeconds", s.pileMeltSeconds);
			read.Float("IceMeltSeconds", s.iceMeltSeconds);
			read.List("HeatKeywords", s.heatKeywords);

			read.List("SnowStatics", s.snowStatics);
			read.List("IceStatics", s.iceStatics);
			read.Float("PileRadius", s.pileRadius);
			read.Int("SweepsToBreakIce", s.sweepsToBreakIce);
			read.Float("RefreshSeconds", s.refreshSeconds);
			read.Bool("SaveOnChange", s.saveOnChange);

			read.List("Brooms", s.brooms);
			read.List("BroomNameKeys", s.broomNameKeys);
			read.List("SnowTextureKeys", s.snowTextureKeys);
			read.List("SnowMaterialKeys", s.snowMaterialKeys);

			read.Bool("EnableSound", s.enableSound);
			read.List("DumpSounds", s.dumpSounds);
			read.List("BroomSounds", s.broomSounds);

			read.String("PhaseTimesFile", s.phaseTimesFile);
		}

		// The AOT-flavoured phase preset overrides the ini hours when present.
		LoadPhaseTimes(s);

		// The terrain carve rides a fake spell-cast event; it needs a real,
		// loaded spell form or it stays off.
		s.carveSpellForm = 0;
		if (s.enableTerrainCarve) {
			if (auto* spell = Lookup(s.carveSpell)) {
				s.carveSpellForm = spell->formID;
			} else {
				spdlog::warn("[Nivalis] CarveSpell '{}' not found; terrain carving disabled", s.carveSpell);
			}
		}

		// Same for the sweep idle: resolve once, warn when absent.
		ResolveSweepIdle();

		spdlog::info("[Nivalis] settings loaded ({} snow statics, {} ice statics, carve {})",
			s.snowStatics.size(), s.iceStatics.size(),
			s.carveSpellForm != 0 ? "on" : "off");
	}

	void ResolveSweepIdle()
	{
		auto& s = Get();

		s.sweepIdleForm = 0;
		if (s.sweepAnywhere && s.sweepKey != 0) {
			if (auto* idle = LookupAs<RE::TESIdleForm>(s.sweepIdle)) {
				s.sweepIdleForm = idle->formID;
			} else {
				spdlog::warn("[Nivalis] SweepIdle '{}' not found; hotkey sweeping plays no animation", s.sweepIdle);
			}
		}
	}
}

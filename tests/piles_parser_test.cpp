// Standalone harness for two pieces of Nivalis that are easy to get wrong and
// expensive in-game: the piles.json parser (a stod crash in 0.3.0) and the
// snow/water/ice rule set. Compiled with cl /EHsc; run directly.
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 1. parser (mirrors Piles.cpp RestoreImpl, v3 record shape)
// ---------------------------------------------------------------------------
struct Pile
{
	float x{ 0 }, y{ 0 }, z{ 0 };
	std::uint32_t world{ 0 }, cell{ 0 };
	std::int32_t size{ 0 };
	int phase{ 0 };
	float born{ 0 }, warm{ 0 }, cold{ 0 }, heat{ 0 }, soakAt{ 0 };
	std::int32_t chops{ 0 };
};

static std::vector<Pile> Parse(std::string text)
{
	std::vector<Pile> piles;

	const auto marker = text.find("\"piles\"");
	const auto arrStart = marker == std::string::npos ? std::string::npos : text.find('[', marker);
	if (arrStart == std::string::npos) {
		return piles;
	}
	const auto arrEnd = text.find(']', arrStart);
	if (arrEnd == std::string::npos) {
		return piles;
	}

	std::size_t pos = arrStart + 1;
	while ((pos = text.find('{', pos)) != std::string::npos && pos < arrEnd) {
		const auto end = text.find('}', pos);
		if (end == std::string::npos || end > arrEnd) {
			break;
		}
		const auto body = text.substr(pos + 1, end - pos - 1);

		Pile pile;
		bool havePos = false;
		std::size_t at = 0;
		while (at < body.size()) {
			const auto keyStart = body.find('"', at);
			if (keyStart == std::string::npos) {
				break;
			}
			const auto keyEnd = body.find('"', keyStart + 1);
			if (keyEnd == std::string::npos) {
				break;
			}
			const auto key = body.substr(keyStart + 1, keyEnd - keyStart - 1);
			const auto colon = body.find(':', keyEnd);
			if (colon == std::string::npos) {
				break;
			}
			const auto valueStart = body.find_first_not_of(" \t", colon + 1);
			if (valueStart == std::string::npos) {
				break;
			}
			const auto valueEnd = body.find_first_of(",", valueStart);
			const auto raw = body.substr(valueStart,
				(valueEnd == std::string::npos ? body.size() : valueEnd) - valueStart);

			double value = 0.0;
			try {
				value = std::stod(raw);
			} catch (const std::exception&) {
				break;
			}

			if (key == "x") { pile.x = static_cast<float>(value); havePos = true; }
			else if (key == "y") { pile.y = static_cast<float>(value); }
			else if (key == "z") { pile.z = static_cast<float>(value); }
			else if (key == "w") { pile.world = static_cast<std::uint32_t>(value); }
			else if (key == "c") { pile.cell = static_cast<std::uint32_t>(value); }
			else if (key == "s") { pile.size = static_cast<std::int32_t>(value); }
			else if (key == "p") { pile.phase = std::clamp(static_cast<int>(value), 0, 2); }
			else if (key == "i") { pile.phase = value >= 0.5 ? 2 : 0; }  // v2 saves
			else if (key == "b") { pile.born = static_cast<float>(value); }
			else if (key == "m") { pile.warm = static_cast<float>(value); }
			else if (key == "f") { pile.cold = static_cast<float>(value); }
			else if (key == "h") { pile.heat = static_cast<float>(value); }
			else if (key == "k") { pile.chops = static_cast<std::int32_t>(value); }
			else if (key == "o") { pile.soakAt = static_cast<float>(value); }

			if (valueEnd == std::string::npos) {
				break;
			}
			at = valueEnd + 1;
		}

		if (havePos) {
			piles.push_back(pile);
		}
		pos = end + 1;
	}

	return piles;
}

// ---------------------------------------------------------------------------
// 2. phase rules (mirrors the accrue/transition block in Piles.cpp Refresh)
// ---------------------------------------------------------------------------
enum : int { kSnow = 0, kWater = 1, kIce = 2 };

struct Sim
{
	int phase{ kSnow };
	float warm{ 0 }, cold{ 0 };
};

static void Step(Sim& s, float delta, bool day, float meltWarm, float freezeCold, float iceWarm)
{
	if (s.phase == kWater || delta <= 0.0f) {
		return;
	}
	if (day) {
		s.warm += delta;
		s.cold = std::max(0.0f, s.cold - delta * 0.25f);
	} else {
		s.cold += delta;
		s.warm = std::max(0.0f, s.warm - delta * 0.25f);
	}

	if (s.phase == kSnow) {
		if (s.warm >= meltWarm) {
			s.phase = kWater;
			s.warm = s.cold = 0;
		} else if (freezeCold > 0.0f && s.cold >= freezeCold) {
			s.phase = kIce;
			s.warm = s.cold = 0;
		}
	} else if (s.phase == kIce) {
		if (s.warm >= iceWarm) {
			s.phase = kWater;
			s.warm = s.cold = 0;
		}
	}
}

int main()
{
	int failed = 0;
	auto check = [&](const char* name, bool ok) {
		if (!ok) ++failed;
		std::cout << (ok ? "PASS " : "FAIL ") << name << "\n";
	};
	auto checkSize = [&](const char* name, std::size_t got, std::size_t want) {
		check(name, got == want);
		if (got != want) std::cout << "      got " << got << " want " << want << "\n";
	};

	// --- parser ---
	checkSize("empty piles array", Parse("{\"version\":3,\"piles\":[]}").size(), 0);
	checkSize("two v3 piles", Parse("{\"version\":3,\"piles\":["
		"{\"x\":100.5,\"y\":-20.25,\"z\":7,\"w\":60,\"c\":0,\"s\":2,\"p\":2,\"b\":5,\"m\":1,\"f\":0,\"h\":3,\"k\":0,\"o\":120},"
		"{\"x\":1,\"y\":2,\"z\":3,\"w\":0,\"c\":750,\"s\":0,\"p\":1,\"b\":9,\"m\":0,\"f\":0,\"h\":0,\"k\":2,\"o\":88}]}").size(), 2);
	checkSize("v2 save still reads", Parse("{\"version\":2,\"piles\":[{\"x\":1,\"y\":2,\"z\":3,\"s\":0,\"i\":1}]}").size(), 1);
	checkSize("garbage tolerated", Parse("{\"version\":3,\"piles\":[{\"x\":\"},{{\"y\":oops}]}").size(), 0);
	checkSize("truncated", Parse("{\"version\":3,\"piles\":[{\"x\":1,\"y\":2").size(), 0);
	checkSize("no wrapper ghost pile", Parse("{\"version\":3,\"piles\":[{\"x\":9,\"y\":9}]}").size(), 1);

	const auto piles = Parse("{\"version\":3,\"piles\":[{\"x\":100.5,\"y\":-20.25,\"s\":2,\"p\":2,\"h\":3.5,\"o\":120}]}");
	check("value round-trip", piles.size() == 1 && piles[0].x == 100.5f && piles[0].y == -20.25f &&
		piles[0].size == 2 && piles[0].phase == kIce && piles[0].heat == 3.5f && piles[0].soakAt == 120.0f);

	// --- phase rules ---
	{
		Sim s;
		// A full afternoon: 4 warm hours, melt threshold 3 -> water.
		for (int i = 0; i < 4 && s.phase == kSnow; ++i) Step(s, 1.0f, true, 3.0f, 5.0f, 2.0f);
		check("day melts snow into water", s.phase == kWater);
	}
	{
		Sim s;
		// A long night: 5 cold hours, freeze threshold 5 -> ice.
		for (int i = 0; i < 5 && s.phase == kSnow; ++i) Step(s, 1.0f, false, 3.0f, 5.0f, 2.0f);
		check("night freezes snow into ice", s.phase == kIce);
	}
	{
		Sim s;
		// Two warm hours then night: the warm progress must decay, not melt.
		Step(s, 2.0f, true, 3.0f, 5.0f, 2.0f);
		Step(s, 1.0f, false, 3.0f, 5.0f, 2.0f);
		check("sunset interruption does not melt", s.phase == kSnow);
	}
	{
		Sim s;
		s.phase = kIce;
		Step(s, 2.0f, true, 3.0f, 5.0f, 2.0f);
		check("ice melts back into water in the warm", s.phase == kWater);
	}
	{
		Sim s;
		s.phase = kWater;
		s.warm = 100.0f;
		Step(s, 10.0f, true, 3.0f, 5.0f, 2.0f);
		check("water never re-phases on its own", s.phase == kWater);
	}
	{
		Sim s;
		// Freeze timer off: a long night leaves snow alone.
		for (int i = 0; i < 20 && s.phase == kSnow; ++i) Step(s, 1.0f, false, 3.0f, 0.0f, 2.0f);
		check("freeze disabled keeps snow", s.phase == kSnow);
	}

	std::cout << (failed == 0 ? "ALL OK\n" : "FAILURES\n");
	return failed == 0 ? 0 : 1;
}

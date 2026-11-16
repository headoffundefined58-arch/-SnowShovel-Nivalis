#include "PCH.h"

#include "Piles.h"

#include "Alchemy.h"
#include "Forms.h"
#include "Settings.h"
#include "Terrain.h"
#include "Water.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
	using namespace Snow;

	std::vector<Pile> g_piles;

	float g_clock{ 0.0f };      // real seconds since load
	float g_timer{ 0.0f };      // refresh cadence
	float g_lastHours{ -1.0f }; // last game-hours snapshot

	// Day/night split: the phase clock only runs one way at a time, so a
	// pile left through the afternoon melts while one left overnight freezes.
	constexpr float kDayStart = 6.0f;
	constexpr float kNightStart = 20.0f;

	// How far a pile may be from the player before its 3D is dropped.
	constexpr float kSpawnRange = 4096.0f;

	[[nodiscard]] std::string StatePath()
	{
		std::filesystem::path base = "Data\\SKSE\\Plugins\\Nivalis";
		if (!std::filesystem::exists(base)) {
			std::filesystem::create_directories(base);
		}
		return (base / "piles.json").string();
	}

	[[nodiscard]] RE::TESObjectREFR* LiveRef(const Pile& a_pile)
	{
		const auto handle = a_pile.ref.native_handle();
		if (handle == 0) {
			return nullptr;
		}
		const auto ref = RE::TESObjectREFR::LookupByHandle(handle);
		return ref.get();
	}

	void Despawn(Pile& a_pile)
	{
		if (auto* ref = LiveRef(a_pile)) {
			ref->Disable();
			ref->SetDelete(true);
		}
		a_pile.ref = RE::ObjectRefHandle{};
	}

	bool Spawn(Pile& a_pile)
	{
		const auto& s = Get();
		// Water is drawn by the bloodpool framework, not by a static.
		if (a_pile.phase == Phase::kWater) {
			return false;
		}
		const auto& list = a_pile.phase == Phase::kIce ? s.iceStatics : s.snowStatics;
		if (list.empty()) {
			return false;
		}

		const auto index = std::clamp<std::int32_t>(a_pile.size, 0, static_cast<std::int32_t>(list.size()) - 1);
		auto* base = LookupAs<RE::TESBoundObject>(list[static_cast<std::size_t>(index)]);
		if (!base) {
			return false;
		}

		auto* handler = RE::TESDataHandler::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		if (!handler || !tes) {
			return false;
		}

		const RE::NiPoint3 pos{ a_pile.x, a_pile.y, a_pile.z };
		auto* cell = tes->GetCell(pos);
		if (!cell) {
			return false;
		}

		auto* world = a_pile.world != 0 ? tes->worldSpace : nullptr;
		const RE::NiPoint3 rot{ 0.0f, 0.0f, 0.0f };

		const auto handle = handler->CreateReferenceAtLocation(
			base, pos, rot, cell, world, nullptr, nullptr, RE::ObjectRefHandle{}, false, false);

		a_pile.ref = handle;
		return a_pile.ref.native_handle() != 0;
	}

	[[nodiscard]] bool NearPlayer(const Pile& a_pile)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return false;
		}

		if (a_pile.world != 0) {
			auto* world = player->GetWorldspace();
			if (!world || world->formID != a_pile.world) {
				return false;
			}
		} else {
			auto* cell = player->GetParentCell();
			if (!cell || cell->formID != a_pile.cell) {
				return false;
			}
		}

		const auto& here = player->GetPosition();
		const float dx = here.x - a_pile.x;
		const float dy = here.y - a_pile.y;
		return std::sqrt(dx * dx + dy * dy) <= kSpawnRange;
	}

	void SnapshotLocation(const RE::NiPoint3& a_pos, Pile& a_out)
	{
		a_out.x = a_pos.x;
		a_out.y = a_pos.y;
		a_out.z = a_pos.z;

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		if (const auto* world = player->GetWorldspace()) {
			a_out.world = world->formID;
		}
		if (const auto* cell = player->GetParentCell()) {
			a_out.cell = cell->formID;
		}
	}

	void RemoveAt(std::size_t a_index)
	{
		Despawn(g_piles[a_index]);
		g_piles.erase(g_piles.begin() + static_cast<std::ptrdiff_t>(a_index));
	}

	// ---- phase transitions -----------------------------------------------

	void ResetPhaseTimers(Pile& a_pile)
	{
		a_pile.warm = 0.0f;
		a_pile.cold = 0.0f;
		a_pile.heat = 0.0f;
		a_pile.chops = 0;
	}

	// Snow or ice turning into water: the static goes away, the bloodpool
	// framework takes over, and the water starts running outward.
	void BecomeWater(std::size_t a_index, bool a_announce)
	{
		auto& pile = g_piles[a_index];
		const auto& s = Get();

		Despawn(pile);
		pile.phase = Phase::kWater;
		ResetPhaseTimers(pile);
		pile.soakAt = g_clock + s.waterSoakSeconds;
		pile.nextPuddle = g_clock + s.puddleEvery;

		const RE::NiPoint3 spot{ pile.x, pile.y, pile.z + 2.0f };
		if (s.enableMeltWater) {
			SpawnPuddleCluster(spot, s.puddleCount, s.puddleSpread);
		}
		if (a_announce) {
			NotifyL("雪化成了水，四处漫开", "The snow melts and runs across the ground");
		}
	}

	void BecomeIce(std::size_t a_index)
	{
		auto& pile = g_piles[a_index];
		Despawn(pile);
		pile.phase = Phase::kIce;
		ResetPhaseTimers(pile);
		Spawn(pile);
		NotifyL("雪堆冻成了冰", "The snow pile has frozen solid");
	}

	// ---- JSON ------------------------------------------------------------
	// Hand rolled: the payload is a flat array of numbers, so pulling in a JSON
	// library for it would cost more than it saves.

	void WriteNumber(std::ostringstream& a_out, const char* a_key, double a_value, bool a_last)
	{
		a_out << '"' << a_key << "\":" << a_value;
		if (!a_last) {
			a_out << ',';
		}
	}

	void Store()
	{
		std::ostringstream out;
		out << "{\"version\":3,\"piles\":[";
		for (std::size_t i = 0; i < g_piles.size(); ++i) {
			const auto& p = g_piles[i];
			if (i != 0) {
				out << ',';
			}
			out << '{';
			WriteNumber(out, "x", p.x, false);
			WriteNumber(out, "y", p.y, false);
			WriteNumber(out, "z", p.z, false);
			WriteNumber(out, "w", p.world, false);
			WriteNumber(out, "c", p.cell, false);
			WriteNumber(out, "s", p.size, false);
			WriteNumber(out, "p", static_cast<double>(static_cast<std::int32_t>(p.phase)), false);
			WriteNumber(out, "b", p.born, false);
			WriteNumber(out, "m", p.warm, false);
			WriteNumber(out, "f", p.cold, false);
			WriteNumber(out, "h", p.heat, false);
			WriteNumber(out, "k", p.chops, false);
			WriteNumber(out, "o", p.soakAt, true);
			out << '}';
		}
		out << "]}";

		std::ofstream file(StatePath(), std::ios::binary);
		if (file.is_open()) {
			file << out.str();
		}
	}

	void RestoreImpl();

	void Restore()
	{
		// Nothing in here may throw: Restore runs from an SKSE callback and an
		// escaping exception is a hard crash.
		try {
			RestoreImpl();
		} catch (const std::exception& e) {
			spdlog::error("[Nivalis] pile restore failed ({}); starting clean", e.what());
			g_piles.clear();
		} catch (...) {
			g_piles.clear();
		}
	}

	void RestoreImpl()
	{
		std::ifstream file(StatePath());
		if (!file.is_open()) {
			return;
		}
		std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };

		g_piles.clear();

		// Only the objects inside "piles":[ ... ] are pile records. The outer
		// wrapper must never be parsed as one: its "piles" value is an array,
		// and stod("[") throws - which escaping a SKSE callback means a crash.
		const auto marker = text.find("\"piles\"");
		const auto arrStart = marker == std::string::npos ? std::string::npos : text.find('[', marker);
		if (arrStart == std::string::npos) {
			return;
		}
		const auto arrEnd = text.find(']', arrStart);
		if (arrEnd == std::string::npos) {
			return;
		}

		std::size_t pos = arrStart + 1;
		while ((pos = text.find('{', pos)) != std::string::npos && pos < arrEnd) {
			const auto end = text.find('}', pos);
			if (end == std::string::npos || end > arrEnd) {
				break;
			}
			const auto body = text.substr(pos + 1, end - pos - 1);

			Pile pile;
			bool havePos = false;  // a record without an "x" is not a pile
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
					break;  // truncated record: "x": with nothing after it
				}
				const auto valueEnd = body.find_first_of(",", valueStart);
				const auto raw = body.substr(valueStart,
					(valueEnd == std::string::npos ? body.size() : valueEnd) - valueStart);

				// Hand edited or truncated files must degrade, not explode.
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
				else if (key == "p") { pile.phase = static_cast<Phase>(std::clamp<std::int32_t>(static_cast<std::int32_t>(value), 0, 2)); }
				else if (key == "i") { pile.phase = value >= 0.5 ? Phase::kIce : Phase::kSnow; }  // v2 saves
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
				g_piles.push_back(pile);
			}
			pos = end + 1;
		}
	}
}

namespace Snow
{
	void Initialize()
	{
		Restore();
		g_lastHours = -1.0f;
		spdlog::info("[Nivalis] {} pile(s) restored from disk", g_piles.size());
	}

	void Shutdown()
	{
		for (auto& pile : g_piles) {
			Despawn(pile);
		}
		Store();
	}

	void Save()
	{
		Store();
	}

	void Reset()
	{
		for (auto& pile : g_piles) {
			Despawn(pile);
		}
		g_piles.clear();
		g_lastHours = -1.0f;
		Store();
	}

	void Refresh()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		const auto& s = Get();
		const auto* calendar = RE::Calendar::GetSingleton();
		const float now = calendar ? calendar->GetHoursPassed() : 0.0f;

		// Elapsed game hours since the last pass. A negative or absurd jump
		// means a save with a different clock: clamp instead of teleporting
		// every pile through several phases at once.
		float delta = g_lastHours < 0.0f ? 0.0f : now - g_lastHours;
		delta = std::clamp(delta, 0.0f, 24.0f);
		g_lastHours = now;

		const float hour = std::fmod(now < 0.0f ? now + 24.0f : now, 24.0f);
		const bool day = hour >= kDayStart && hour < kNightStart;

		bool dirty = false;
		for (std::size_t i = 0; i < g_piles.size();) {
			auto& pile = g_piles[i];

			// --- phase timers (game clock, the AOT time base) ---
			if (PhaseClockAvailable() && pile.phase != Phase::kWater && delta > 0.0f) {
				if (day) {
					pile.warm += delta;
					pile.cold = std::max(0.0f, pile.cold - delta * 0.25f);
				} else {
					pile.cold += delta;
					pile.warm = std::max(0.0f, pile.warm - delta * 0.25f);
				}

				if (pile.phase == Phase::kSnow) {
					if (pile.warm >= s.meltWarmHours) {
						BecomeWater(i, true);
						dirty = true;
						++i;
						continue;
					}
					if (s.freezeColdHours > 0.0f && pile.cold >= s.freezeColdHours) {
						BecomeIce(i);
						dirty = true;
					}
				} else if (pile.phase == Phase::kIce) {
					if (pile.warm >= s.iceWarmHours) {
						BecomeWater(i, true);
						dirty = true;
						++i;
						continue;
					}
				}
			}

			// --- standing water: spread a little, then soak away ---
			if (pile.phase == Phase::kWater) {
				if (s.enableMeltWater && g_clock >= pile.nextPuddle) {
					pile.nextPuddle = g_clock + s.puddleEvery;
					SpawnPuddleCluster(RE::NiPoint3{ pile.x, pile.y, pile.z + 2.0f }, 1, s.puddleSpread);
				}
				if (g_clock >= pile.soakAt) {
					RemoveAt(i);  // absorbed by the land
					dirty = true;
					continue;
				}
			}

			// --- 3D presence ---
			if (pile.phase != Phase::kWater) {
				const bool near = NearPlayer(pile);
				const bool live = LiveRef(pile) != nullptr;
				if (near && !live) {
					Spawn(pile);
				} else if (!near && live) {
					Despawn(pile);
				}
			}

			++i;
		}

		if (dirty && s.saveOnChange) {
			Store();
		}
	}

	void AdvanceGameClock(float a_delta)
	{
		g_clock += a_delta;
	}

	void Tick(float a_delta)
	{
		AdvanceGameClock(a_delta);

		if (g_piles.empty()) {
			return;
		}

		g_timer += a_delta;
		if (g_timer < Get().refreshSeconds) {
			return;
		}
		g_timer = 0.0f;
		Refresh();
	}

	std::int32_t FindNear(const RE::NiPoint3& a_pos, float a_radius)
	{
		std::int32_t best = -1;
		float bestDist = a_radius * a_radius;
		for (std::size_t i = 0; i < g_piles.size(); ++i) {
			const auto& p = g_piles[i];
			const float dx = p.x - a_pos.x;
			const float dy = p.y - a_pos.y;
			const float dist = dx * dx + dy * dy;
			if (dist <= bestDist) {
				bestDist = dist;
				best = static_cast<std::int32_t>(i);
			}
		}
		return best;
	}

	bool SweepAt(const RE::NiPoint3& a_pos)
	{
		const auto& s = Get();
		const auto index = FindNear(a_pos, s.pileRadius);
		if (index < 0) {
			return false;
		}

		auto& pile = g_piles[static_cast<std::size_t>(index)];

		if (pile.phase == Phase::kWater) {
			// Brushing at melt water just scatters it; leave it to soak in.
			return false;
		}

		if (pile.phase == Phase::kIce) {
			// Frozen crust: a broom chips at it, but it takes real effort.
			pile.chops += 1;
			if (pile.chops >= std::max(1, s.sweepsToBreakIce)) {
				RemoveAt(static_cast<std::size_t>(index));
				PlaySound(s.dumpSounds);
				NotifyL("冰被敲碎了", "The ice breaks apart");
				if (s.saveOnChange) {
					Save();
				}
				return true;
			}
			NotifyL("冻住了，得多扫几下才碎", "Frozen - a few more strokes to break it");
			PlaySound(s.broomSounds);
			if (s.saveOnChange) {
				Save();
			}
			return true;
		}

		Despawn(pile);
		if (pile.size <= 0) {
			g_piles.erase(g_piles.begin() + index);
		} else {
			pile.size -= 1;
			Spawn(pile);
		}

		PlaySound(s.broomSounds);
		return true;
	}

	bool GatherAt(const RE::NiPoint3& a_pos)
	{
		const auto& s = Get();

		if (!IsSnowGround(a_pos) || FindNear(a_pos, s.gatherRadius) >= 0) {
			return false;
		}

		if (static_cast<std::int32_t>(g_piles.size()) >= s.maxPiles) {
			// Drop the oldest so the file cannot grow without bound.
			RemoveAt(0);
		}

		const auto* calendar = RE::Calendar::GetSingleton();

		Pile pile;
		SnapshotLocation(a_pos, pile);
		pile.size = 0;
		pile.phase = Phase::kSnow;
		pile.born = calendar ? calendar->GetHoursPassed() : 0.0f;
		ResetPhaseTimers(pile);

		g_piles.push_back(pile);
		Spawn(g_piles.back());
		return true;
	}

	int HeatAt(const RE::NiPoint3& a_pos, float a_radius, float a_delta)
	{
		const auto& s = Get();
		const auto index = FindNear(a_pos, a_radius);
		if (index < 0) {
			return 0;
		}

		auto& pile = g_piles[static_cast<std::size_t>(index)];
		if (pile.phase == Phase::kWater) {
			return 0;  // already water; the sun/fire cannot melt it further
		}

		pile.heat += a_delta;
		const float need = pile.phase == Phase::kIce ? s.iceMeltSeconds : s.pileMeltSeconds;
		if (pile.heat < need) {
			return 0;
		}

		BecomeWater(static_cast<std::size_t>(index), true);
		return 1;
	}

	std::size_t Count()
	{
		return g_piles.size();
	}

	std::size_t CountPhase(Phase a_phase)
	{
		std::size_t n = 0;
		for (const auto& p : g_piles) {
			if (p.phase == a_phase) {
				++n;
			}
		}
		return n;
	}

	const std::vector<Pile>& All()
	{
		return g_piles;
	}
}

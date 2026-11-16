#include "PCH.h"

#include "Heat.h"

#include "Forms.h"
#include "Piles.h"
#include "Settings.h"
#include "Terrain.h"
#include "Water.h"

#include <algorithm>
#include <cctype>
#include <random>
#include <string>
#include <vector>
#include <windows.h>

namespace
{
	using namespace Snow;

	float g_scanTimer{ 0.0f };
	float g_waterTimer{ 0.0f };
	float g_heatTimer{ 0.0f };

	// Heat sources found around the player during the last scan. Positions
	// only: the scan runs on the main thread and everything below reads these.
	std::vector<RE::NiPoint3> g_heatSpots;

	std::mt19937 g_rng{ ::GetTickCount() };

	[[nodiscard]] std::string Lower(std::string a_text)
	{
		std::transform(a_text.begin(), a_text.end(), a_text.begin(),
			[](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
		return a_text;
	}

	[[nodiscard]] bool KeywordHit(const std::string& a_text)
	{
		if (a_text.empty()) {
			return false;
		}
		const auto lowered = Lower(a_text);
		for (const auto& key : Get().heatKeywords) {
			if (!key.empty() && lowered.find(Lower(key)) != std::string::npos) {
				return true;
			}
		}
		return false;
	}

	// Editor ids need po3's Tweaks to resolve, so names and model paths carry
	// the decision for everything else.
	[[nodiscard]] bool IsHeatSource(RE::TESObjectREFR& a_ref)
	{
		auto* base = a_ref.GetBaseObject();
		if (!base) {
			return false;
		}

		if (const char* name = base->GetName(); name && *name && KeywordHit(name)) {
			return true;
		}

		if (auto* model = base->As<RE::TESModel>(); model && model->GetModel() && KeywordHit(model->GetModel())) {
			return true;
		}

		if (const char* edid = base->GetFormEditorID(); edid && *edid && KeywordHit(edid)) {
			return true;
		}

		return false;
	}

	void ScanHeatSources(RE::TESObjectREFR* a_center)
	{
		g_heatSpots.clear();

		auto* tes = RE::TES::GetSingleton();
		if (!tes || !a_center) {
			return;
		}

		tes->ForEachReferenceInRange(a_center, Get().meltProbeRadius * 3.0f, [&](RE::TESObjectREFR* a_ref) {
			if (a_ref && IsHeatSource(*a_ref)) {
				g_heatSpots.push_back(a_ref->GetPosition());
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
	}

	// A wet patch just past the fire's edge: not under the flames, but where
	// the snow the fire has been eating would drain to.
	void AmbientMelt(const RE::NiPoint3& a_center)
	{
		const auto& s = Get();
		if (g_heatSpots.empty() || !DbfReady()) {
			return;
		}

		std::uniform_int_distribution<std::size_t> pick(0, g_heatSpots.size() - 1);
		std::uniform_real_distribution<float> ring(0.45f, 1.0f);
		std::uniform_real_distribution<float> angle(0.0f, 6.2831853f);

		const auto& fire = g_heatSpots[pick(g_rng)];
		const float r = s.meltProbeRadius * ring(g_rng);
		const float a = angle(g_rng);
		RE::NiPoint3 spot{ fire.x + std::cos(a) * r, fire.y + std::sin(a) * r, fire.z };

		if (!IsSnowGround(spot)) {
			// Only spill where there is actually snow to melt.
			return;
		}

		// Start a touch above the fire's own ground level; the framework
		// raycasts down and conforms to the terrain itself.
		spot.z = fire.z + 4.0f;
		SpawnPuddle(spot, -1.0f);
	}

	// Piles sitting in heat give up their water. Snow goes first, ice takes
	// longer; either way the pile turns into run-off instead of vanishing.
	void PileHeatPass(float a_delta)
	{
		const auto& s = Get();
		if (g_heatSpots.empty()) {
			return;
		}

		for (const auto& fire : g_heatSpots) {
			if (HeatAt(fire, s.meltProbeRadius, a_delta) == 1) {
				return;  // one pile per pass keeps the melt visibly gradual
			}
		}
	}
}

namespace Snow
{
	void InitializeHeat()
	{
		InitializeWater();
	}

	void TickHeat(float a_delta)
	{
		const auto& s = Get();
		if (!s.enable) {
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto& pos = player->GetPosition();

		g_scanTimer += a_delta;
		if (g_scanTimer >= 2.0f) {
			g_scanTimer = 0.0f;
			ScanHeatSources(player);
		}

		if (g_heatSpots.empty()) {
			return;
		}

		g_waterTimer += a_delta;
		if (s.enableMeltWater && DbfReady() && g_waterTimer >= s.meltWaterInterval) {
			g_waterTimer = 0.0f;
			AmbientMelt(pos);
		}

		g_heatTimer += a_delta;
		if (g_heatTimer >= 1.0f) {
			const float step = g_heatTimer;
			g_heatTimer = 0.0f;
			PileHeatPass(step);
		}
	}
}

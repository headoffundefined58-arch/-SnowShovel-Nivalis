#include "PCH.h"

#include "Water.h"

#include "DBFAPI.h"
#include "Settings.h"

#include <cmath>
#include <random>

namespace
{
	using namespace Snow;

	DBF_API::Interface* g_dbf{ nullptr };
	bool g_logged{ false };
	bool g_profileWarned{ false };

	std::mt19937 g_rng{ ::GetTickCount() };

	[[nodiscard]] float Rand(float a_lo, float a_hi)
	{
		std::uniform_real_distribution<float> dist(a_lo, a_hi);
		return dist(g_rng);
	}
}

namespace Snow
{
	bool InitializeWater()
	{
		if (g_dbf) {
			return true;
		}

		g_dbf = static_cast<DBF_API::Interface*>(DBF_API::GetAPI());
		if (g_dbf) {
			spdlog::info("[Nivalis] Dynamic Bloodpool Framework API acquired");
		} else if (!g_logged) {
			g_logged = true;
			spdlog::info("[Nivalis] Dynamic Bloodpool Framework not loaded; melt stays dry");
		}
		return g_dbf != nullptr;
	}

	bool DbfReady()
	{
		return g_dbf != nullptr;
	}

	void SpawnPuddle(const RE::NiPoint3& a_pos, float a_scale)
	{
		if (!g_dbf) {
			return;
		}

		const auto& s = Get();

		DBF_API::Interface::Parameters params;
		params.profileID = s.puddleProfileID;
		params.waitForStableOrigin = false;
		params.override.position = a_pos;
		if (a_scale > 0.0f) {
			params.override.scale = a_scale;
		}
		params.override.durationMult = 1.0f;

		if (!g_dbf->SpawnBloodpool(params) && !g_profileWarned) {
			g_profileWarned = true;
			spdlog::warn("[Nivalis] SpawnBloodpool('{}') failed - is the puddle profile JSON installed?",
				s.puddleProfileID);
		}
	}

	void SpawnPuddleCluster(const RE::NiPoint3& a_pos, std::int32_t a_count, float a_spread)
	{
		if (!g_dbf || a_count <= 0) {
			return;
		}

		// The pile's own spot first, then the run-off ring around it.
		SpawnPuddle(a_pos, -1.0f);

		for (std::int32_t i = 1; i < a_count; ++i) {
			const float angle = Rand(0.0f, 6.2831853f);
			const float radius = a_spread * std::sqrt(Rand(0.25f, 1.0f));
			RE::NiPoint3 spot{
				a_pos.x + std::cos(angle) * radius,
				a_pos.y + std::sin(angle) * radius,
				a_pos.z + 2.0f,
			};
			SpawnPuddle(spot, -1.0f);
		}
	}
}

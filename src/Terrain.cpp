#include "PCH.h"

#include "Terrain.h"

#include "Settings.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace
{
	using namespace Snow;

	[[nodiscard]] std::string Lower(std::string a_value)
	{
		std::transform(a_value.begin(), a_value.end(), a_value.begin(),
			[](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
		return a_value;
	}

	// "Landscape\\Snow\\snow01.dds" -> "snow01.dds"
	[[nodiscard]] std::string BaseName(std::string a_path)
	{
		const auto slash = a_path.find_last_of("\\/");
		return slash == std::string::npos ? a_path : a_path.substr(slash + 1);
	}

	[[nodiscard]] bool ContainsAny(const std::string& a_haystack, const std::vector<std::string>& a_keys)
	{
		if (a_haystack.empty()) {
			return false;
		}
		for (const auto& key : a_keys) {
			if (!key.empty() && a_haystack.find(Lower(key)) != std::string::npos) {
				return true;
			}
		}
		return false;
	}
}

namespace Snow
{
	bool IsSnowGround(const RE::NiPoint3& a_pos)
	{
		const auto* tes = RE::TES::GetSingleton();
		if (!tes) {
			return false;
		}

		const auto* land = tes->GetLandTexture(a_pos);
		if (!land) {
			return false;
		}

		const auto& s = Get();

		if (land->textureSet) {
			const auto* name = land->textureSet->textures[0].textureName.c_str();
			if (name && *name) {
				if (ContainsAny(Lower(BaseName(name)), s.snowTextureKeys)) {
					return true;
				}
			}
		}

		// Textures are the primary signal; the material type catches retextures
		// that kept the stock material but swapped the diffuse.
		if (land->materialType) {
			if (ContainsAny(Lower(land->materialType->GetName() ? land->materialType->GetName() : ""),
					s.snowMaterialKeys)) {
				return true;
			}
		}

		return false;
	}

	bool GroundPointAhead(RE::TESObjectREFR* a_actor, float a_distance, RE::NiPoint3& a_out)
	{
		if (!a_actor) {
			return false;
		}

		const auto& pos = a_actor->GetPosition();

		// GetAngle() is the facing on the horizontal plane; the camera pitch is
		// deliberately ignored so the swing always lands on the ground you are
		// standing at rather than at the horizon.
		float heading = a_actor->GetAngleZ();
		const RE::NiPoint3 ahead{
			pos.x + std::sin(heading) * a_distance,
			pos.y + std::cos(heading) * a_distance,
			pos.z,
		};

		auto* tes = RE::TES::GetSingleton();
		if (!tes) {
			return false;
		}

		float height = ahead.z;
		if (!tes->GetLandHeight(ahead, height)) {
			// Interior or an unloaded cell: fall back to the actor's own feet,
			// which keeps the action harmless instead of spawning a pile in the
			// void.
			height = pos.z;
		}

		a_out = RE::NiPoint3{ ahead.x, ahead.y, height };
		return true;
	}
}

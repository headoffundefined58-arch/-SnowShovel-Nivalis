#pragma once

#include <RE/N/NiPoint3.h>

namespace RE
{
	class TESObjectREFR;
}

namespace Snow
{
	// True when the landscape texture under a point reads as snow. Mirrors what
	// the deformable-terrain mod does with its own Surfaces.ini: the texture
	// name is the ground truth, the material type is the fallback.
	[[nodiscard]] bool IsSnowGround(const RE::NiPoint3& a_pos);

	// The point a swing lands on: a short step ahead of the actor, dropped onto
	// the terrain. Aiming with the camera alone would make shovelling a
	// first-person floor-staring exercise.
	[[nodiscard]] bool GroundPointAhead(RE::TESObjectREFR* a_actor, float a_distance, RE::NiPoint3& a_out);
}

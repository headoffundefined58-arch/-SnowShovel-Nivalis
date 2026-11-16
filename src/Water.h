#pragma once

#include <RE/N/NiPoint3.h>

#include <cstdint>

namespace Snow
{
	// The water side of the loop, on top of the Dynamic Bloodpool Framework.
	// DBF is what actually draws and fades a puddle, which is exactly the
	// behaviour wanted here: melt water spreads, sits, then soaks away.
	// Everything degrades to a no-op when the framework is not installed.

	bool InitializeWater();
	[[nodiscard]] bool DbfReady();

	// One puddle at the given spot. a_scale <= 0 lets the profile decide.
	void SpawnPuddle(const RE::NiPoint3& a_pos, float a_scale = -1.0f);

	// a_count puddles scattered inside a_spread around a_pos: a melt pool
	// that has run outward instead of a single tidy disc.
	void SpawnPuddleCluster(const RE::NiPoint3& a_pos, std::int32_t a_count, float a_spread);
}

#pragma once

namespace Snow
{
	// The heat side of the loop:
	//  - heat sources near the player are scanned by keyword (same list the
	//    deformable-terrain mod uses, so both mods agree on what is hot);
	//  - a pile that sits in that heat gives up its water: snow and ice both
	//    turn into run-off, which the bloodpool framework draws;
	//  - ambient meltwater seeps out around fires that stand on snow.
	//
	// The deformable-terrain mod already melts the snow *field* around heat
	// sources on its own; this only adds the water it cannot express.
	void InitializeHeat();
	void TickHeat(float a_delta);
}

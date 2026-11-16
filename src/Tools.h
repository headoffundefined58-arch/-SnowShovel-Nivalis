#pragma once

namespace Snow
{
	enum class Tool
	{
		kNone = 0,
		kBroom,
	};

	// What the player is holding: equipped items first, then known inventory
	// items. Stock brooms are MISC objects that cannot swing, so the inventory
	// check is what makes them usable at all.
	[[nodiscard]] Tool CurrentTool(bool a_equippedOnly);
}

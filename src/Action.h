#pragma once

namespace Snow
{
	// Registers the swing listener on the player and the hotkey listener on
	// the input device manager. Safe to call once the game data is loaded;
	// repeated calls are ignored.
	void InitializeAction();

	// Drives the swing floor timer; called from the per-frame hook.
	void AdvanceClock(float a_delta);

	// Start/stop a sweep session on demand (the SweepKey hotkey). Works
	// anywhere: strokes simply no-op where there is no snow.
	//
	// a_ignorePause is for the in-menu toggle: opening that menu pauses the
	// game, so the pause guard would otherwise make the button do nothing.
	void ToggleSweepAnywhere(bool a_ignorePause = false);
}

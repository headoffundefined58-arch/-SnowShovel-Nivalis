#pragma once

namespace Snow
{
	// Alchemy of Time ships no plugin API: it is a content driven item-aging
	// engine (yml presets + a game-clock scheduler). The integration here is
	// therefore at the level of its *concept*, which is what this framework
	// needs anyway:
	//
	//   * the phase clock runs on the same in-game hours AOT schedules on;
	//   * the phase timings live in Data\SKSE\Plugins\Nivalis\phases.yml,
	//     written in AOT's preset shape, so the numbers are tuned in the same
	//     idiom AOT users already know (and can be lifted into an AOT preset);
	//   * AOT's presence is the framework's declared dependency: with
	//     PhaseRequireAOT = true (default) the snow/water/ice cycle only runs
	//     while AlchemyOfTime.dll is loaded.
	bool AotLoaded();

	// True when the phase machine may advance: enabled in the ini and, when
	// the AOT dependency is on, actually backed by Alchemy of Time.
	[[nodiscard]] bool PhaseClockAvailable();

	void LogAotStatus();
}

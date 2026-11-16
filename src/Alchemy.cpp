#include "PCH.h"

#include "Alchemy.h"

#include "Settings.h"

#include <windows.h>

namespace Snow
{
	bool AotLoaded()
	{
		return ::GetModuleHandleW(L"AlchemyOfTime.dll") != nullptr;
	}

	bool PhaseClockAvailable()
	{
		const auto& s = Get();
		if (!s.phaseEnabled) {
			return false;
		}
		if (s.phaseRequireAot && !AotLoaded()) {
			return false;
		}
		return true;
	}

	void LogAotStatus()
	{
		const auto& s = Get();
		if (AotLoaded()) {
			spdlog::info("[Nivalis] Alchemy of Time detected - the snow/water/ice clock is running");
		} else if (s.phaseRequireAot) {
			spdlog::warn("[Nivalis] Alchemy of Time missing - phases are OFF "
						 "(set PhaseRequireAOT = false to run without it)");
		} else {
			spdlog::info("[Nivalis] Alchemy of Time not loaded; the phase clock runs on the game clock anyway");
		}
		if (!s.enable) {
			spdlog::info("[Nivalis] plugin disabled by INI");
		}
	}
}

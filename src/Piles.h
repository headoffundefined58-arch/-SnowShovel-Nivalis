#pragma once

#include <RE/B/BSPointerHandle.h>
#include <RE/N/NiPoint3.h>

#include <cstdint>
#include <vector>

namespace Snow
{
	// The three states a gathered snow layer moves through. The world
	// reference is a temporary runtime object: the record is what survives,
	// and the 3D is rebuilt whenever the player comes back.
	enum class Phase : std::int32_t
	{
		kSnow = 0,   // 松雪堆
		kWater = 1,  // 融成的水（血池框架画，慢慢渗走）
		kIce = 2,    // 冻成的冰
	};

	struct Pile
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
		std::uint32_t world{ 0 };  // worldspace form id, 0 for interiors
		std::uint32_t cell{ 0 };   // cell form id
		std::int32_t size{ 0 };    // index into the static lists

		Phase phase{ Phase::kSnow };
		float born{ 0.0f };       // game hours when created
		float warm{ 0.0f };       // warm (day) game hours accrued in this phase
		float cold{ 0.0f };       // cold (night) game hours accrued in this phase
		float heat{ 0.0f };       // real seconds spent next to a heat source
		float soakAt{ 0.0f };     // real-clock second the water soaks away
		float nextPuddle{ 0.0f }; // real-clock second of the next run-off puddle
		std::int32_t chops{ 0 };  // sweeps already landed on a frozen pile

		RE::ObjectRefHandle ref{};
	};

	void Initialize();
	void Shutdown();
	void Save();
	// Wipes every record: a new game should not inherit drifts from the save
	// you were playing five minutes ago.
	void Reset();
	void Tick(float a_delta);
	void Refresh();

	// Advances the game clock used by the phase timers; called each frame so
	// the elapsed-hours maths stays cheap and jitter free.
	void AdvanceGameClock(float a_delta);

	// Actions. Each returns true when it changed something, which is what
	// decides whether a sound plays and whether the state gets written back.
	bool SweepAt(const RE::NiPoint3& a_pos);

	// Sweeping loose snow into a heap: creates a small pile at a_pos when the
	// ground there is snowy and nothing else is piled within gather radius.
	bool GatherAt(const RE::NiPoint3& a_pos);

	// Heat side. Accumulates a_delta seconds of heat on the nearest pile.
	// Returns 1 when that pile turned into water, 2 when it was consumed
	// (soaked away), 0 when nothing happened.
	int HeatAt(const RE::NiPoint3& a_pos, float a_radius, float a_delta);

	// Nearest pile within a radius, or -1.
	std::int32_t FindNear(const RE::NiPoint3& a_pos, float a_radius);

	[[nodiscard]] std::size_t Count();
	[[nodiscard]] std::size_t CountPhase(Phase a_phase);
	[[nodiscard]] const std::vector<Pile>& All();
}

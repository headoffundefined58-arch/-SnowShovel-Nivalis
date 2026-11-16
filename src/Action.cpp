#include "PCH.h"

#include "Action.h"

#include "Forms.h"
#include "MenuBridge.h"
#include "Piles.h"
#include "Settings.h"
#include "Terrain.h"
#include "Tools.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace
{
	using namespace Snow;

	// Two strokes cannot land on the same frame, and power attacks fire several
	// graph events for one motion, so a short floor keeps one swing to one sweep.
	constexpr float kSwingFloor = 0.5f;

	// After the player asks the broom to be put away, ignore a sweep-idle
	// event for this long: the idle's exit transition can emit one more
	// "start" on its way out, which would immediately reopen the session.
	constexpr float kStopGuard = 0.75f;

	float g_lastSwing{ -1000.0f };
	float g_clock{ 0.0f };
	float g_lastToggle{ -1000.0f };
	float g_lastUserStop{ -1000.0f };

	// ---- idle playback ---------------------------------------------------
	// The sweep-anywhere toggle drives the same vanilla idle the sweeping
	// mod uses (IdleLooseSweepingStart). Playing it makes the behavior graph
	// emit "idleloosesweepingstart", which the sink below already treats as
	// a sweep session - so the hotkey and the sweeping mod share one code
	// path.
	bool PlaySweepIdle(RE::PlayerCharacter* a_player)
	{
		const auto& s = Get();
		if (s.sweepIdleForm == 0) {
			return false;
		}

		auto* idle = RE::TESForm::LookupByID<RE::TESIdleForm>(s.sweepIdleForm);
		if (!idle) {
			return false;
		}

		auto* process = a_player->GetActorRuntimeData().currentProcess;
		return process && process->PlayIdle(a_player, idle, nullptr);
	}

	void StopSweepIdle(RE::PlayerCharacter* a_player)
	{
		// Stopping takes TWO calls, and only the second one used to be here.
		//
		// StopCurrentIdle clears the idle manager entry, but the loose
		// sweeping idle is a behavior-graph state: it keeps playing (broom
		// still swinging) until something tells the graph to leave it. The
		// only thing that did was a movement input - which is exactly why
		// jumping "worked" and the hotkey did not.
		//
		// The vanilla sweeping flow ends it with the "IdleStop" graph event,
		// which is the transition the idle itself listens for. The event name
		// is an ini knob (SweepStopEvent) in case a moveset mod does not eat
		// IdleStop; IdleForceDefaultState is the hard fallback.
		const auto& stopEvent = Get().sweepStopEvent;
		const bool stopped = !stopEvent.empty() && a_player->NotifyAnimationGraph(stopEvent.c_str());
		if (!stopped && Get().debug) {
			spdlog::debug("[Nivalis] stop event '{}' was not consumed by the graph", stopEvent);
		}

		if (auto* process = a_player->GetActorRuntimeData().currentProcess) {
			process->StopCurrentIdle(a_player, true);
		}
	}

	// ---- sweep session ---------------------------------------------------
	// The sweeping mod (Sweeping Organizes Stuff) plays the vanilla idle
	// IdleLooseSweepingStart when a broom is equipped; its behavior graph then
	// emits "idleLooseSweepingStart", and any key press ends it with the
	// "IdleStop" event. Riding those events means the broom sweeps snow with
	// zero interference in how that mod does everything else.
	bool  g_sweeping{ false };
	float g_nextStroke{ 0.0f };
	float g_sessionEnd{ 0.0f };
	float g_lastCarve{ -1000.0f };
	bool  g_gatheredThisSession{ false };
	bool  g_carvedThisSession{ false };

	void CarveTerrain(RE::PlayerCharacter* a_player)
	{
		const auto& s = Get();
		if (s.carveSpellForm == 0) {
			return;
		}

		// A synthetic spell-cast event: the game itself casts nothing, but the
		// deformable-terrain mod listens on this event and turns it into a
		// snow stamp, which reads as the broom having cleared the ground.
		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->SendSpellCastEvent(RE::NiPointer<RE::TESObjectREFR>(a_player), s.carveSpellForm);
		}
	}

	// One sweep stroke around the player: pile work first, then the terrain,
	// then the heap the loose snow gathers into.
	void DoSweepStroke(RE::PlayerCharacter* a_player, bool a_allowGather)
	{
		const auto& s = Get();
		const auto& pos = a_player->GetPosition();

		bool changed = SweepAt(pos);

		RE::NiPoint3 target{};
		const bool haveTarget = GroundPointAhead(a_player, s.forwardDistance, target);
		const bool snowyHere = haveTarget && IsSnowGround(target);

		if (snowyHere && s.enableTerrainCarve &&
			g_clock - g_lastCarve >= s.carveCooldown) {
			g_lastCarve = g_clock;
			CarveTerrain(a_player);
			changed = true;
		}

		if (a_allowGather && s.gatherPiles && !g_gatheredThisSession && snowyHere) {
			if (GatherAt(target)) {
				g_gatheredThisSession = true;
				NotifyL("扫拢了一小堆雪", "You gather the loose snow into a heap");
				changed = true;
			}
		}

		// One notice per session keeps the corner of the screen quiet while
		// the strokes keep landing every couple of seconds.
		if (snowyHere && s.enableTerrainCarve && !g_carvedThisSession) {
			g_carvedThisSession = true;
			NotifyL("扫开了脚下的雪", "You sweep the snow aside");
		}

		if (changed) {
			if (!snowyHere) {
				PlaySound(Get().broomSounds);
			}
			if (s.debug) {
				spdlog::debug("[Nivalis] sweep stroke at ({:.0f},{:.0f},{:.0f}) snow={} piles={}",
					pos.x, pos.y, pos.z, snowyHere, Count());
			}
			if (s.saveOnChange) {
				Save();
			}
		} else if (g_sweeping) {
			// Hotkey sweeping has no physical broom swinging, so every
			// session stroke gets the rustle regardless of what it hit.
			PlaySound(Get().broomSounds);
		}
	}

	void BeginSweepSession(const Settings& a_s)
	{
		g_sweeping = true;
		g_nextStroke = g_clock + a_s.sweepStartDelay;
		g_sessionEnd = g_clock + a_s.sweepTimeout;
		g_gatheredThisSession = false;
		g_carvedThisSession = false;
	}

	class SnowEventSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(
			const RE::BSAnimationGraphEvent* a_event,
			RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
		{
			if (!a_event || !Get().enable) {
				return RE::BSEventNotifyControl::kContinue;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || a_event->holder != player) {
				return RE::BSEventNotifyControl::kContinue;
			}

			const auto& s = Get();
			const char* tag = a_event->tag.c_str();
			std::string lowered = tag ? Lower(tag) : std::string{};

			if (lowered == "idleloosesweepingstart") {
				// The idle can re-announce itself on the way out, so a fresh
				// "start" right after the player put the broom away is the
				// exit transition, not a new session.
				if (g_clock - g_lastUserStop < kStopGuard) {
					if (s.debug) {
						spdlog::debug("[Nivalis] sweep idle re-fired within the stop guard; ignored");
					}
					return RE::BSEventNotifyControl::kContinue;
				}

				// Bonus path: if another mod (or a mod-added idle) starts the
				// vanilla sweep idle, ride along - but only with a broom on
				// hand, because carrying one is this framework's hard rule.
				if (CurrentTool(false) == Tool::kBroom) {
					BeginSweepSession(s);
					if (s.debug) {
						spdlog::debug("[Nivalis] sweep session started (sweep idle)");
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			if (lowered == "idlestop") {
				g_sweeping = false;
				return RE::BSEventNotifyControl::kContinue;
			}

			// Optional fallback: a swing with a broom in hand also sweeps.
			if (!g_sweeping && s.triggerOnAttack && IsSwing(lowered)) {
				if (g_clock - g_lastSwing < kSwingFloor) {
					return RE::BSEventNotifyControl::kContinue;
				}
				g_lastSwing = g_clock;

				if (CurrentTool(true) == Tool::kBroom) {
					DoSweepStroke(player, false);
				}
			}

			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		[[nodiscard]] static std::string Lower(std::string a_value)
		{
			std::transform(a_value.begin(), a_value.end(), a_value.begin(),
				[](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
			return a_value;
		}

		[[nodiscard]] static bool IsSwing(const std::string& a_tag)
		{
			// "attackStart" covers hands and one handed tools; the shout and
			// bash variants are deliberately excluded so blocking never sweeps.
			return a_tag.find("attackstart") != std::string::npos ||
			       a_tag.find("weaponswing") != std::string::npos;
		}
	};

	SnowEventSink g_sink;

	// ---- sweep hotkey (the primary trigger) ------------------------------
	// Nivalis owns its sweeping outright: press the key to sweep where you
	// stand, press it again to stop. No dependency on any other sweeping mod.
	// Where there is no snow the strokes simply do nothing.
	namespace Snow
	{
		void ToggleSweepAnywhere(bool a_ignorePause);
	}

	class SweepKeySink : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(
			RE::InputEvent* const* a_events,
			RE::BSTEventSource<RE::InputEvent*>*) override
		{
			const auto& s = Get();
			if (!a_events || !s.enable) {
				return RE::BSEventNotifyControl::kContinue;
			}

			// While the settings menu is up the keyboard belongs to it: no
			// sweeping, and no stray toggles while the player types a key.
			// A pending rebind is handled first, because that capture happens
			// precisely while the menu is open.
			if (SnowMenu::CapturePending()) {
				for (auto event = *a_events; event; event = event->next) {
					const auto* button = event->AsButtonEvent();
					if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard || !button->IsDown()) {
						continue;
					}
					const auto code = static_cast<int>(button->GetIDCode());
					if (code == 1) {
						SnowMenu::CancelKeyCapture();
					} else {
						SnowMenu::AcceptCapturedKey(code);
					}
					break;
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			if (SnowMenu::MenuBlocking()) {
				return RE::BSEventNotifyControl::kContinue;
			}

			if (!s.sweepAnywhere || s.sweepKey == 0) {
				return RE::BSEventNotifyControl::kContinue;
			}

			// IsDown() is true only on the first press event, so key repeat
			// never double-fires; the clock guard covers multi-device ghosts.
			for (auto event = *a_events; event; event = event->next) {
				const auto* button = event->AsButtonEvent();
				if (button && button->GetDevice() == RE::INPUT_DEVICE::kKeyboard &&
					button->GetIDCode() == static_cast<std::uint32_t>(s.sweepKey) &&
					button->IsDown()) {
					ToggleSweepAnywhere();
					break;
				}
			}

			return RE::BSEventNotifyControl::kContinue;
		}
	};

	SweepKeySink g_keySink;
}

namespace Snow
{
	void ToggleSweepAnywhere(bool a_ignorePause)
	{
		const auto& s = Get();
		if (!s.enable || !s.sweepAnywhere || s.sweepKey == 0) {
			return;
		}

		// Debounce: key events can arrive through several paths at once.
		if (!a_ignorePause && g_clock - g_lastToggle < 0.3f) {
			return;
		}
		g_lastToggle = g_clock;

		// Menus and pause screens swallow gameplay; don't sweep into them.
		// The in-menu toggle passes a_ignorePause because its own menu is the
		// thing pausing the game.
		if (!a_ignorePause) {
			if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
				return;
			}
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		// Already sweeping: a second press puts the broom down. The stop has
		// to end the *animation*, not just the session timer, or the broom
		// keeps swinging until the player jumps.
		if (g_sweeping) {
			g_sweeping = false;
			g_lastUserStop = g_clock;
			StopSweepIdle(player);
			NotifyL("收起了扫帚", "You put the broom away");
			if (s.debug) {
				spdlog::debug("[Nivalis] sweep session stopped by hotkey");
			}
			return;
		}

		if (player->IsInCombat()) {
			NotifyL("战斗中没法扫地", "Not while fighting");
			return;
		}

		// Hard rule (deliberately not exposed in the menu): you need a broom.
		if (s.sweepNeedBroom && CurrentTool(false) != Tool::kBroom) {
			NotifyL("背包里没有扫帚", "You have no broom");
			return;
		}

		BeginSweepSession(s);
		if (!PlaySweepIdle(player)) {
			// Sweeping still works (strokes, carve, piles) - there is just no
			// idle animation. Say so once instead of looking broken.
			static bool warned = false;
			if (!warned) {
				warned = true;
				spdlog::warn("[Nivalis] sweep idle could not be played (SweepIdle '{}'); "
							 "sweeping continues without the animation",
					s.sweepIdle);
			}
		}
		NotifyL("开始扫地", "You start sweeping");
		if (s.debug) {
			spdlog::debug("[Nivalis] sweep session started by hotkey");
		}
	}

	void InitializeAction()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			spdlog::error("[Nivalis] no player, sweep listener not registered");
			return;
		}
		player->AddAnimationGraphEventSink(&g_sink);

		if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
			input->AddEventSink(&g_keySink);
			spdlog::info("[Nivalis] sweep key listener registered");
		}

		spdlog::info("[Nivalis] sweep listener registered");
	}

	// Called from the update hook: advances the clock and drives any active
	// sweep session.
	void AdvanceClock(float a_delta)
	{
		g_clock += a_delta;

		if (!g_sweeping) {
			return;
		}

		const auto& s = Get();
		if (g_clock >= g_sessionEnd) {
			g_sweeping = false;
			if (s.debug) {
				spdlog::debug("[Nivalis] sweep session timed out");
			}
			return;
		}

		if (g_clock >= g_nextStroke) {
			g_nextStroke = g_clock + s.sweepInterval;
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				DoSweepStroke(player, true);
			}
		}
	}
}

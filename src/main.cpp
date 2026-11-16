#include "PCH.h"

#include "Action.h"
#include "Alchemy.h"
#include "Heat.h"
#include "MenuBridge.h"
#include "Piles.h"
#include "Settings.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <windows.h>

namespace
{
	using UpdateFn = void (*)(RE::Actor*, float);

	// Actor::Update is virtual; slot 0x0AD on SE/AE. Patching the player's own
	// vtable keeps this independent of any Address Library id.
	constexpr std::size_t kUpdateSlot = 0x0AD;

	UpdateFn g_original{ nullptr };

	void Thunk(RE::Actor* a_this, float a_delta)
	{
		if (g_original) {
			g_original(a_this, a_delta);
		}

		if (!a_this || a_this != RE::PlayerCharacter::GetSingleton()) {
			return;
		}

		Snow::AdvanceClock(a_delta);
		Snow::TickHeat(a_delta);
		Snow::Tick(a_delta);
	}

	// CommonLibSSE-NG never installs a file sink: its default logger only has
	// the console sink, which a game process does not have. Without this the
	// plugin is completely silent in My Games\...\SKSE.
	void SetupLog()
	{
		auto directory = SKSE::log::log_directory();
		if (!directory) {
			return;
		}

		*directory /= "Nivalis.log";

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(directory->string(), true);
		sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

		auto logger = std::make_shared<spdlog::logger>("snow", std::move(sink));
		logger->set_level(spdlog::level::debug);
		logger->flush_on(spdlog::level::debug);

		spdlog::set_default_logger(std::move(logger));
		spdlog::set_level(spdlog::level::debug);
	}

	bool InstallUpdateHook()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			spdlog::error("[Nivalis] PlayerCharacter unavailable, no per-frame hook");
			return false;
		}

		auto** vtable = *reinterpret_cast<void***>(player);
		void** slot = &vtable[kUpdateSlot];

		void* original = *slot;
		if (!original) {
			spdlog::error("[Nivalis] Actor::Update slot is null");
			return false;
		}

		g_original = reinterpret_cast<UpdateFn>(original);

		DWORD oldProtect = 0;
		if (!::VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
			spdlog::error("[Nivalis] VirtualProtect failed ({})", ::GetLastError());
			return false;
		}

		*slot = reinterpret_cast<void*>(&Thunk);

		DWORD restored = 0;
		::VirtualProtect(slot, sizeof(void*), oldProtect, &restored);
		return true;
	}

	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			Snow::LoadSettings();
			Snow::Initialize();
			Snow::InitializeAction();
			Snow::InitializeHeat();
			Snow::LogAotStatus();
			SnowMenu::InitializeMenu();
			if (InstallUpdateHook()) {
				spdlog::info("[Nivalis] per-frame hook installed");
			}
			break;
		case SKSE::MessagingInterface::kNewGame:
			Snow::LoadSettings();
			Snow::InitializeHeat();
			Snow::Reset();
			spdlog::info("[Nivalis] new game, pile records cleared");
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			Snow::LoadSettings();
			Snow::InitializeHeat();
			Snow::Refresh();
			spdlog::info("[Nivalis] save loaded, {} pile(s)", Snow::Count());
			break;
		case SKSE::MessagingInterface::kSaveGame:
			Snow::Save();
			break;
		default:
			break;
		}
	}
}

SKSEPluginInfo(
	.Version = REL::Version{ 0, 5, 1, 0 },
	.Name = "雪相 Nivalis",
	.Author = "Icarus",
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary);

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	SetupLog();
	Snow::LoadSettings();

	if (const auto* messaging = SKSE::GetMessagingInterface()) {
		messaging->RegisterListener(OnSKSEMessage);
	}

	spdlog::info("[Nivalis] loaded, version 0.5.1");
	return true;
}

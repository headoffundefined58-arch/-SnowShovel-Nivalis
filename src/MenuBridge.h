#pragma once

#include <string>

namespace SnowMenu
{
	// Plain-data snapshot of the editable settings. Deliberately free of any
	// RE types: the MMSF menu translation unit cannot see CommonLibSSE
	// headers (the menu API header pulls in <windows.h>, which collides with
	// CommonLibSSE-NG's parallel REX::W32 implementation under the PCH), so
	// everything crosses this bridge by value.
	//
	// The menu stays deliberately small: only the knobs a player realistically
	// wants in-game. Everything else lives in Nivalis.ini. The broom
	// requirement in particular is a fixed rule, not a toggle.

	// Language indexes shared by the menu UI and the ini "Language" key.
	inline constexpr int kLangCN = 0;
	inline constexpr int kLangEN = 1;

	struct Values
	{
		int language{ kLangCN };

		bool enable{ true };
		bool notify{ true };
		bool debug{ false };

		// [Sweep]
		bool  sweepAnywhere{ true };
		int   sweepKey{ 23 };  // DirectInput scan code; 23 = I
		float sweepStartDelay{ 0.9f };
		float sweepInterval{ 2.2f };
		float sweepTimeout{ 30.0f };

		// [Terrain]
		bool  enableTerrainCarve{ true };
		float carveCooldown{ 2.4f };
		bool  gatherPiles{ true };

		// [Phases]
		bool  phaseEnabled{ true };
		float meltWarmHours{ 3.0f };
		float freezeColdHours{ 5.0f };
		float iceWarmHours{ 2.0f };
		float waterSoakSeconds{ 90.0f };

		// [Heat] / puddles
		bool  enableMeltWater{ true };
		int   puddleCount{ 3 };
		float puddleSpread{ 110.0f };
		float meltProbeRadius{ 200.0f };
		float pileMeltSeconds{ 6.0f };
		float iceMeltSeconds{ 14.0f };
	};

	struct Status
	{
		int  piles{ 0 };
		int  snow{ 0 };
		int  water{ 0 };
		int  ice{ 0 };
		bool aot{ false };     // Alchemy of Time loaded
		bool phases{ false };  // phase clock actually running
		bool dbf{ false };     // bloodpool framework acquired
		bool carve{ false };   // terrain carve spell resolved
	};

	Values Snapshot();
	Status GetStatus();

	// True when the ini says Language = en.
	[[nodiscard]] bool LanguageIsEn();

	// Copies the values into the live settings. Memory only; the ini is not
	// touched, so dragging a slider cannot hammer the disk.
	void Apply(const Values& a_values);

	// Patches Data\SKSE\Plugins\Nivalis.ini: replaces the first matching key
	// line, appends the key when absent, keeps everything else.
	void Persist(const Values& a_values);

	// Queues a sweep toggle onto the main thread; safe to call from render
	// threads.
	void ToggleSweepNow();

	// ---- sweep hotkey rebinding ------------------------------------------
	// The menu cannot read the keyboard by itself (the ImGui translation unit
	// is compiled without CommonLib), so the actual capture happens in the
	// gameplay input sink: the menu only raises the request, the sink answers
	// it with the next key press. Esc cancels.
	void BeginKeyCapture();
	void CancelKeyCapture();
	[[nodiscard]] bool CapturePending();

	// Called by the input sink when a key resolved a pending capture: stores
	// it, re-resolves the sweep idle and writes SweepKey to the ini.
	void AcceptCapturedKey(int a_scanCode);

	// Target of the SKSE Menu Framework input hook. The framework hands the
	// menu TU a RE::InputEvent* it cannot read (no CommonLib there), so the
	// pointer is forwarded here for parsing. Returns true when the press was
	// consumed by a pending capture.
	bool HandleMenuInput(void* a_event);

	// True while SKSE Menu Framework has a blocking window open, i.e. while
	// the player is in a menu and gameplay keys must stand down.
	[[nodiscard]] bool MenuBlocking();

	// Registers the menu with SKSE Menu Framework. Called once at kDataLoaded;
	// implemented in MenuMMSF.cpp.
	void InitializeMenu();
}

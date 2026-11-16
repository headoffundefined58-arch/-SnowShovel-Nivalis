#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Snow
{
	// Nivalis 雪相 —— 相变时长、触发与联动参数。
	// 全部可在 Data\SKSE\Plugins\Nivalis.ini 手改（保存即生效），
	// 也可在游戏内 SKSE 菜单框架里改（改完自动写回 ini）。
	struct Settings
	{
		// [General]
		bool         enable{ true };
		bool         debug{ false };
		bool         notify{ true };
		std::int32_t maxPiles{ 200 };
		std::string  language{ "cn" };  // 菜单语言：cn | en

		// [Trigger]
		// 扫地入口：热键（默认 I）+ 可选攻击键兜底。
		// 背包必须有扫帚是硬性条件（SweepNeedBroom），不放进菜单。
		bool         sweepAnywhere{ true };
		std::int32_t sweepKey{ 23 };   // DirectInput 扫描码；23 = I，0 = 关
		bool         sweepNeedBroom{ true };  // 硬性规则：背包里要有扫帚
		bool         triggerOnAttack{ false };  // 挥击也扫（需手持扫帚）
		std::string  sweepIdle{ "Skyrim.esm:0640FE" };  // IdleLooseSweepingStart

		// [Sweep]
		float sweepStartDelay{ 0.9f };      // idle 起来、扫帚落地
		float sweepInterval{ 2.2f };        // 两次扫动的间隔（真实秒）
		float sweepTimeout{ 30.0f };        // 事件丢失时的兜底
		// 打断扫地用的行为图事件。IdleStop 是标准做法；个别动作模组不吃这个事件时，
		// 可以换成 IdleForceDefaultState（硬切回默认姿态）。
		std::string sweepStopEvent{ "IdleStop" };

		// [Terrain] 动态地形变化（硬需求）
		bool         enableTerrainCarve{ true };
		std::string  carveSpell{ "Skyrim.esm:02C8B4" };
		float        carveCooldown{ 2.4f };
		bool         gatherPiles{ true };   // 把松雪扫拢成堆
		float        gatherRadius{ 260.0f };
		float        forwardDistance{ 110.0f };

		// [Phases] 雪 -> 水 -> 冰 相变（游戏时钟驱动，与时间炼成同一套）
		bool  phaseEnabled{ true };
		bool  phaseRequireAot{ true };      // 依赖时间炼成：缺失则相变停用
		float meltWarmHours{ 3.0f };        // 白天累积多少游戏小时后化成水
		float freezeColdHours{ 5.0f };      // 夜里累积多少游戏小时后冻成冰
		float iceWarmHours{ 2.0f };         // 冰在白天累积多少小时后化成水
		float waterSoakSeconds{ 90.0f };    // 水洼存留多久（真实秒）后被土地吸收
		float puddleEvery{ 18.0f };         // 水相期间每隔多久再漫出一滩（流的到处都是）
		std::int32_t puddleCount{ 3 };      // 每次出水同时生成几滩
		float        puddleSpread{ 110.0f };  // 水洼散布半径
		std::string  puddleProfileID{ "NivalisMelt" };  // 血池框架 profile

		// [Heat] 热源融雪（软需求：动态血池框架）
		bool  enableMeltWater{ true };
		float meltWaterInterval{ 9.0f };    // 环境融雪水洼节奏（真实秒）
		float meltProbeRadius{ 200.0f };    // 距热源多近算"热"
		float pileMeltSeconds{ 6.0f };      // 雪堆受热多少秒化成水
		float iceMeltSeconds{ 14.0f };      // 冰堆受热多少秒化成水
		std::vector<std::string> heatKeywords;

		// [Piles]
		std::vector<std::string> snowStatics;   // 雪堆外观（小->大）
		std::vector<std::string> iceStatics;    // 冰堆外观（小->大）
		float        pileRadius{ 90.0f };
		std::int32_t sweepsToBreakIce{ 3 };
		float        refreshSeconds{ 2.0f };
		bool         saveOnChange{ true };

		// [Tools]
		std::vector<std::string> brooms;
		std::vector<std::string> broomNameKeys;

		// [Snow]
		std::vector<std::string> snowTextureKeys;
		std::vector<std::string> snowMaterialKeys;

		// [Sound]
		bool enableSound{ true };
		std::vector<std::string> dumpSounds;
		std::vector<std::string> broomSounds;

		// [AOT] 相变时长预设（AOT 风格 yml），随文件热重载
		std::string phaseTimesFile{ "Data\\SKSE\\Plugins\\Nivalis\\phases.yml" };

		// ---- 加载时解析一次 ----
		std::uint32_t carveSpellForm{ 0 };
		std::uint32_t sweepIdleForm{ 0 };
	};

	Settings& Get();

	void LoadSettings();

	// Resolves SweepIdle into a form id. LoadSettings calls it once, and the
	// hotkey rebind in the menu calls it again: the idle is only resolved
	// while hotkey sweeping is actually on, so flipping the key (or enabling
	// the feature) at runtime has to redo it.
	void ResolveSweepIdle();
}

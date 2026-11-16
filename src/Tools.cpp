#include "PCH.h"

#include "Tools.h"

#include "Forms.h"
#include "Settings.h"

namespace
{
	using namespace Snow;

	[[nodiscard]] bool IsBroomForm(RE::TESForm* a_form)
	{
		if (!a_form) {
			return false;
		}
		const auto& s = Get();
		for (const auto& spec : s.brooms) {
			if (const auto* known = Lookup(spec); known && known->formID == a_form->formID) {
				return true;
			}
		}
		return NameMatches(a_form, s.broomNameKeys);
	}
}

namespace Snow
{
	Tool CurrentTool(bool a_equippedOnly)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return Tool::kNone;
		}

		for (const bool left : { false, true }) {
			if (IsBroomForm(player->GetEquippedObject(left))) {
				return Tool::kBroom;
			}
		}

		if (a_equippedOnly) {
			return Tool::kNone;
		}

		// Only the configured form ids are tested here: walking the whole
		// inventory on every swing is not worth it, and any modded tool the
		// plugin should recognise can simply be listed in the INI.
		const auto& s = Get();
		for (const auto& spec : s.brooms) {
			if (auto* bound = LookupAs<RE::TESBoundObject>(spec); bound && player->GetItemCount(bound) > 0) {
				return Tool::kBroom;
			}
		}

		return Tool::kNone;
	}
}

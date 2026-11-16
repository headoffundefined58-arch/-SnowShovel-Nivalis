#pragma once

#include <RE/T/TESBoundObject.h>
#include <RE/T/TESForm.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Snow
{
	// "Skyrim.esm:0F5D05" or a bare "0F5D05" (which defaults to Skyrim.esm).
	struct FormSpec
	{
		std::string mod{ "Skyrim.esm" };
		std::uint32_t id{ 0 };
	};

	FormSpec ParseSpec(const std::string& a_text);

	RE::TESForm* Lookup(const std::string& a_spec);

	template <class T>
	T* LookupAs(const std::string& a_spec)
	{
		auto* form = Lookup(a_spec);
		return form ? form->As<T>() : nullptr;
	}

	// Plays the first entry of the list that resolves, at the player.
	void PlaySound(const std::vector<std::string>& a_specs);

	// True when the form's display name or editor id contains any keyword.
	// Editor ids only resolve with powerofthree's Tweaks installed, so this
	// degrades to name matching rather than failing outright.
	bool NameMatches(RE::TESForm* a_form, const std::vector<std::string>& a_keys);

	void Notify(const char* a_text);

	// Bilingual notice: shows the English string when the menu language is
	// set to English, otherwise the Chinese one.
	void NotifyL(const char* a_zh, const char* a_en);
}

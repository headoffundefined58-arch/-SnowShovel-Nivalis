#include "PCH.h"

#include "Forms.h"

#include "Settings.h"

#include <algorithm>
#include <cctype>

namespace
{
	using namespace Snow;

	[[nodiscard]] std::string Lower(std::string a_value)
	{
		std::transform(a_value.begin(), a_value.end(), a_value.begin(),
			[](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
		return a_value;
	}

	[[nodiscard]] std::string Trim(std::string a_value)
	{
		const auto first = a_value.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) {
			return {};
		}
		const auto last = a_value.find_last_not_of(" \t\r\n");
		return a_value.substr(first, last - first + 1);
	}
}

namespace Snow
{
	// stoul throws on anything that is not a hex digit, and an exception
	// escaping a SKSE callback is a hard crash. A malformed INI entry has to
	// degrade to "form not found" instead.
	[[nodiscard]] std::uint32_t ParseHex(const std::string& a_text)
	{
		try {
			std::size_t consumed = 0;
			const auto v = std::stoul(a_text, &consumed, 16);
			if (a_text[0] == '-') {
				return 0;
			}
			return static_cast<std::uint32_t>(v);
		} catch (const std::exception&) {
			return 0;
		}
	}

	FormSpec ParseSpec(const std::string& a_text)
	{
		FormSpec spec;
		const auto value = Trim(a_text);
		const auto colon = value.find(':');
		if (colon == std::string::npos) {
			spec.id = ParseHex(value);
			return spec;
		}
		spec.mod = Trim(value.substr(0, colon));
		spec.id = ParseHex(Trim(value.substr(colon + 1)));
		return spec;
	}

	RE::TESForm* Lookup(const std::string& a_spec)
	{
		if (a_spec.empty()) {
			return nullptr;
		}

		const auto spec = ParseSpec(a_spec);
		if (spec.id == 0) {
			return nullptr;
		}

		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			return nullptr;
		}

		auto* form = handler->LookupForm(spec.id, spec.mod);
		if (!form) {
			// TESDataHandler::LookupForm has shown itself unreliable here
			// (the small-file index leaks into the compiled id for regular
			// plugins), so always give the raw full-id lookup a chance too.
			// This covers vanilla ids, index-0 mods and raw FE:xx ids.
			form = RE::TESForm::LookupByID(spec.id);
		}
		return form;
	}

	void PlaySound(const std::vector<std::string>& a_specs)
	{
		if (!Get().enableSound) {
			return;
		}

		for (const auto& spec : a_specs) {
			const auto* form = Lookup(spec);
			if (!form) {
				continue;
			}
			auto* manager = RE::BSAudioManager::GetSingleton();
			if (manager && manager->Play(form->GetFormID())) {
				return;
			}
		}
	}

	bool NameMatches(RE::TESForm* a_form, const std::vector<std::string>& a_keys)
	{
		if (!a_form || a_keys.empty()) {
			return false;
		}

		std::string haystack;
		if (const auto* name = a_form->GetName(); name && *name) {
			haystack += Lower(name);
		}
		if (const auto* edid = a_form->GetFormEditorID(); edid && *edid) {
			haystack += Lower(edid);
		}
		if (haystack.empty()) {
			return false;
		}

		for (const auto& key : a_keys) {
			if (haystack.find(Lower(key)) != std::string::npos) {
				return true;
			}
		}
		return false;
	}

	void Notify(const char* a_text)
	{
		if (!Get().notify || !a_text) {
			return;
		}
		RE::DebugNotification(a_text);
	}

	void NotifyL(const char* a_zh, const char* a_en)
	{
		const auto& s = Get();
		if (!s.notify) {
			return;
		}
		const bool english = Lower(s.language) == "en";
		const char* text = english ? a_en : a_zh;
		if (text) {
			RE::DebugNotification(text);
		}
	}
}

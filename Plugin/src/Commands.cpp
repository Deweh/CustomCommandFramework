#include "Commands.h"
#include "polyhook2/Detour/x64Detour.hpp"

namespace Util
{
	static std::vector<CCF::simple_string_view> sv_vec_to_ssv_vec(const std::vector<std::string_view>& a_sourceVec)
	{
		std::vector<CCF::simple_string_view> result;
		result.reserve(a_sourceVec.size());
		for (auto& s : a_sourceVec) {
			result.push_back(CCF::simple_string_view(s.data(), s.size()));
		}
		return result;
	}

	std::vector<std::string_view> str_split(const std::string_view& s, const std::string_view& delimiter, const std::optional<char>& escapeChar)
	{
		std::vector<std::string_view> substrings;
		size_t start = 0;
		size_t end = 0;
		bool escaped = false;

		while (end < s.length()) {
			if (escapeChar.has_value() && s[end] == escapeChar) {
				escaped = !escaped;
			} else if (!escaped && s.substr(end, delimiter.length()) == delimiter) {
				substrings.push_back(s.substr(start, end - start));
				start = end + delimiter.length();
				end = start - 1;
			}
			end++;
		}

		if (start < s.length()) {
			substrings.push_back(s.substr(start));
		}

		if (escapeChar.has_value()) {
			for (auto& substring : substrings) {
				if (substring.size() > 1 && substring.front() == escapeChar.value() && substring.back() == escapeChar.value()) {
					substring.remove_prefix(1);
					substring.remove_suffix(1);
				}
			}
		}

		return substrings;
	}

	std::string str_tolower(const std::string_view s)
	{
		std::string result(s);
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
		return result;
	}
}

namespace Commands
{
	RE::NiPointer<RE::TESObjectREFR> GetRefrFromHandle(uint32_t handle)
	{
		RE::NiPointer<RE::TESObjectREFR> result;
		REL::Relocation<void(RE::NiPointer<RE::TESObjectREFR>&, uint32_t*)> func(REL::ID(72399));
		func(result, &handle);
		return result;
	}

	RE::NiPointer<RE::TESObjectREFR> GetConsoleRefr()
	{
		REL::Relocation<uint64_t**> consoleReferencesManager(REL::ID(879512));
		REL::Relocation<uint32_t* (uint64_t*, uint32_t*)> GetConsoleHandle(REL::ID(166314));
		uint32_t outId = 0;
		GetConsoleHandle(*consoleReferencesManager, &outId);
		return GetRefrFromHandle(outId);
	}

	class Interface : public CCF::ConsoleInterface
	{
	public:
		virtual RE::NiPointer<RE::TESObjectREFR> GetSelectedReference() {
			return GetConsoleRefr();
		}

		virtual RE::TESForm* HexStrToForm(const CCF::simple_string_view& a_str) {
			uint32_t formId;
			try {
				formId = std::stoul(std::string{ a_str.get() }, nullptr, 16);
			} catch (std::exception) {
				return nullptr;
			}

			return RE::TESForm::LookupByID(formId);
		}

		virtual void PrintLn(const CCF::simple_string_view& a_txt) {
			if (log != nullptr) {
				if (!printedDefault) {
					log->Print(defText);
					printedDefault = true;
				}
				log->Print(std::string{ a_txt.get() }.c_str());
			}
		}

		virtual void PreventDefaultPrint() {
			printedDefault = true;
		}

		void PrintDefault() {
			if (!printedDefault && log != nullptr) {
				log->Print(defText);
				printedDefault = true;
			}
		}

		void Reset(const char* a_txt) {
			if (log == nullptr) {
				log = RE::ConsoleLog::GetSingleton();
			}
			defText = a_txt;
			printedDefault = false;
		}

		RE::ConsoleLog* log = nullptr;
		const char* defText = nullptr;
		bool printedDefault = false;
	};

	typedef int64_t (*ExecuteCommandFunc)(void*, const char*);

	std::unique_ptr<PLH::x64Detour> ExecuteCommandDetour = nullptr;
	ExecuteCommandFunc OriginalExecuteCommand;
	std::mutex regLock;
	std::unordered_map<std::string, CCF::CommandCallback> registrations;
	Interface intfc;

	int64_t ExecuteCommand(void* a1, const char* a_cmd, ...)
	{
		if (!a_cmd) {
			return OriginalExecuteCommand(a1, a_cmd);
		}

		std::string_view cmdView(a_cmd);
		auto args = Util::str_split(cmdView, " ", '\"');
		std::unique_lock l{ regLock };
		if (auto iter = registrations.find(Util::str_tolower(args[0])); iter != registrations.end()) {
			args.erase(args.begin());
			auto argsSSV = Util::sv_vec_to_ssv_vec(args);
			CCF::simple_array<CCF::simple_string_view> argsArr(argsSSV);

			intfc.Reset(a_cmd);
			iter->second(argsArr, a_cmd, &intfc);
			intfc.PrintDefault();
			return 0;
		} else {
			return OriginalExecuteCommand(a1, a_cmd);
		}
	}

	void InstallHooks()
	{
		REL::Relocation<uintptr_t> hookLoc{ REL::ID(166307) };
		ExecuteCommandDetour = std::make_unique<PLH::x64Detour>(
			static_cast<uint64_t>(hookLoc.address()),
			reinterpret_cast<uint64_t>(&ExecuteCommand),
			reinterpret_cast<uint64_t*>(&OriginalExecuteCommand));
		ExecuteCommandDetour->hook();

		INFO("Installed command hook.");
	}

	void RegisterCommand(const char* a_name, CCF::CommandCallback a_func)
	{
		std::unique_lock l{ regLock };
		std::string nameStr = Util::str_tolower(a_name);
		if (nameStr.empty()) {
			return;
		}

		if (registrations.contains(nameStr)) {
			WARN("A plugin tried to register command '{}', but that command has already been registered", a_name);
			return;
		}
		registrations.insert(std::make_pair(nameStr, a_func));
		INFO("Command '{}' has been registered.", a_name);
	}
}

DLLEXPORT void RegisterCommand(const char* a_name, CCF::CommandCallback a_func)
{
	Commands::RegisterCommand(a_name, a_func);
}

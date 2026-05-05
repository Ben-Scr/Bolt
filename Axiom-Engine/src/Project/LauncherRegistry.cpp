#include "pch.hpp"
#include "Project/LauncherRegistry.hpp"
#include "Project/AxiomProject.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/Directory.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Core/Log.hpp"

#include <algorithm>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>

namespace Axiom {
	namespace {
		bool ToLocalTime(std::time_t value, std::tm& outTime) {
#if defined(_WIN32)
			return localtime_s(&outTime, &value) == 0;
#else
			return localtime_r(&value, &outTime) != nullptr;
#endif
		}
	}

	static std::string NowISO8601() {
		auto t = std::time(nullptr);
		std::tm tm{};
		if (!ToLocalTime(t, tm)) {
			return {};
		}
		std::stringstream ss;
		ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
		return ss.str();
	}

	std::string LauncherRegistry::GetRegistryPath() {
		return Path::Combine(
			Path::GetSpecialFolderPath(SpecialFolder::LocalAppData),
			"Axiom", "launcher.json");
	}

	void LauncherRegistry::Load() {
		m_Projects.clear();
		std::string path = GetRegistryPath();

		if (!File::Exists(path)) return;

		Json::Value root;
		std::string parseError;
		const std::string jsonText = File::ReadAllText(path);
		if (!Json::TryParse(jsonText, root, &parseError) || !root.IsArray()) {
			AIM_CORE_WARN_TAG("LauncherRegistry", "Failed to parse '{}': {}", path, parseError);
			return;
		}

		for (const Json::Value& item : root.GetArray()) {
			if (!item.IsObject()) {
				continue;
			}

			LauncherProjectEntry entry;
			if (const Json::Value* nameValue = item.FindMember("name")) {
				entry.Name = nameValue->AsStringOr();
			}
			if (const Json::Value* pathValue = item.FindMember("path")) {
				entry.Path = pathValue->AsStringOr();
			}
			if (const Json::Value* lastOpenedValue = item.FindMember("lastOpened")) {
				entry.LastOpened = lastOpenedValue->AsStringOr();
			}

			if (!entry.Name.empty() && !entry.Path.empty()) {
				m_Projects.push_back(std::move(entry));
			}
		}

		std::sort(m_Projects.begin(), m_Projects.end(),
			[](const auto& a, const auto& b) { return a.LastOpened > b.LastOpened; });
	}

	void LauncherRegistry::Save() {
		std::string path = GetRegistryPath();

		// Ensure parent directory exists
		auto parent = std::filesystem::path(path).parent_path();
		if (!std::filesystem::exists(parent))
			std::filesystem::create_directories(parent);

		Json::Value root = Json::Value::MakeArray();
		for (const LauncherProjectEntry& project : m_Projects) {
			Json::Value item = Json::Value::MakeObject();
			item.AddMember("name", project.Name);
			item.AddMember("path", project.Path);
			item.AddMember("lastOpened", project.LastOpened);
			root.Append(std::move(item));
		}
		File::WriteAllText(path, Json::Stringify(root, true));
	}

	void LauncherRegistry::AddProject(const std::string& name, const std::string& path) {
		for (auto& p : m_Projects) {
			if (p.Path == path) {
				p.LastOpened = NowISO8601();
				// Re-sort so the bumped entry surfaces in the recent-projects view
				// without waiting for the next UpdateLastOpened call.
				std::sort(m_Projects.begin(), m_Projects.end(),
					[](const auto& a, const auto& b) { return a.LastOpened > b.LastOpened; });
				return;
			}
		}

		LauncherProjectEntry entry;
		entry.Name = name;
		entry.Path = path;
		entry.LastOpened = NowISO8601();
		m_Projects.insert(m_Projects.begin(), entry);
	}

	void LauncherRegistry::RemoveProject(const std::string& path) {
		m_Projects.erase(
			std::remove_if(m_Projects.begin(), m_Projects.end(),
				[&](const auto& e) { return e.Path == path; }),
			m_Projects.end());
	}

	void LauncherRegistry::UpdateLastOpened(const std::string& path) {
		for (auto& p : m_Projects) {
			if (p.Path == path) {
				p.LastOpened = NowISO8601();
				break;
			}
		}
		std::sort(m_Projects.begin(), m_Projects.end(),
			[](const auto& a, const auto& b) { return a.LastOpened > b.LastOpened; });
	}

	void LauncherRegistry::ValidateAll() {
		m_Projects.erase(
			std::remove_if(m_Projects.begin(), m_Projects.end(),
				[](const auto& e) { return !AxiomProject::Validate(e.Path); }),
			m_Projects.end());
	}

} // namespace Axiom

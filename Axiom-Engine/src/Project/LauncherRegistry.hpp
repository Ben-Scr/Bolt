#pragma once
#include "Core/Export.hpp"
#include "Project/LauncherProjectEntry.hpp"

#include <string>
#include <vector>

namespace Axiom {

	class AXIOM_API LauncherRegistry {
	public:
		void Load();
		void Save();

		const std::vector<LauncherProjectEntry>& GetProjects() const { return m_Projects; }
		void AddProject(const std::string& name, const std::string& path);
		void RemoveProject(const std::string& path);
		void UpdateLastOpened(const std::string& path);
		void ValidateAll();

	private:
		static std::string GetRegistryPath();
		std::vector<LauncherProjectEntry> m_Projects;
	};

} // namespace Axiom

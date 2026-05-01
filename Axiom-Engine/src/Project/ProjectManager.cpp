#include "pch.hpp"
#include "Project/ProjectManager.hpp"
#include "Assets/AssetRegistry.hpp"

namespace Axiom {

	std::unique_ptr<AxiomProject> ProjectManager::s_CurrentProject = nullptr;

	void ProjectManager::SetCurrentProject(std::unique_ptr<AxiomProject> project) {
		s_CurrentProject = std::move(project);
		AssetRegistry::MarkDirty();
		AssetRegistry::Sync();
	}

	AxiomProject* ProjectManager::GetCurrentProject() {
		return s_CurrentProject.get();
	}

	bool ProjectManager::HasProject() {
		return s_CurrentProject != nullptr;
	}

} // namespace Axiom

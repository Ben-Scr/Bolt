#pragma once
#include "Project/AxiomProject.hpp"
#include "Core/Export.hpp"
#include <memory>

namespace Axiom {

	class AXIOM_API ProjectManager {
	public:
		static void SetCurrentProject(std::unique_ptr<AxiomProject> project);
		static AxiomProject* GetCurrentProject();
		static bool HasProject();

	private:
		static std::unique_ptr<AxiomProject> s_CurrentProject;
	};

} // namespace Axiom

#pragma once

#include "Events/AxiomEvent.hpp"

#include <string>
#include <utility>
#include <vector>

namespace Axiom {

	class FileDropEvent : public AxiomEvent {
	public:
		explicit FileDropEvent(std::vector<std::string> paths)
			: m_Paths(std::move(paths)) {
		}

		const std::vector<std::string>& GetPaths() const { return m_Paths; }
		int GetCount() const { return static_cast<int>(m_Paths.size()); }

		AIM_EVENT_CLASS_TYPE(FileDrop)
		AIM_EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		std::vector<std::string> m_Paths;
	};

} // namespace Axiom

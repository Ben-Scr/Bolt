#pragma once

#include "Events/AxiomEvent.hpp"

#include <string>

namespace Axiom {

	class SceneEvent : public AxiomEvent {
	public:
		const std::string& GetSceneName() const { return m_SceneName; }

		AIM_EVENT_CLASS_CATEGORY(EventCategoryScene)

	protected:
		explicit SceneEvent(const std::string& sceneName)
			: m_SceneName(sceneName) {
		}

		std::string m_SceneName;
	};

} // namespace Axiom

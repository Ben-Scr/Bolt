#pragma once

#include "Events/SceneEvent.hpp"

namespace Axiom {

	class ScenePreStartEvent : public SceneEvent {
	public:
		explicit ScenePreStartEvent(const std::string& sceneName)
			: SceneEvent(sceneName) {
		}

		AIM_EVENT_CLASS_TYPE(ScenePreStart)
	};

} // namespace Axiom

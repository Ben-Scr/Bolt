#pragma once

#include "Events/SceneEvent.hpp"

namespace Axiom {

	class ScenePreStopEvent : public SceneEvent {
	public:
		explicit ScenePreStopEvent(const std::string& sceneName)
			: SceneEvent(sceneName) {
		}

		AIM_EVENT_CLASS_TYPE(ScenePreStop)
	};

} // namespace Axiom

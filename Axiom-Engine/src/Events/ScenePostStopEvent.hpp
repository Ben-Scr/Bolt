#pragma once

#include "Events/SceneEvent.hpp"

namespace Axiom {

	class ScenePostStopEvent : public SceneEvent {
	public:
		explicit ScenePostStopEvent(const std::string& sceneName)
			: SceneEvent(sceneName) {
		}

		AIM_EVENT_CLASS_TYPE(ScenePostStop)
	};

} // namespace Axiom

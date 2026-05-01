#pragma once

#include "Events/SceneEvent.hpp"

namespace Axiom {

	class ScenePostStartEvent : public SceneEvent {
	public:
		explicit ScenePostStartEvent(const std::string& sceneName)
			: SceneEvent(sceneName) {
		}

		AIM_EVENT_CLASS_TYPE(ScenePostStart)
	};

} // namespace Axiom

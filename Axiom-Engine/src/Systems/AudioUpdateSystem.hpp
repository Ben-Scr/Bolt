#pragma once
#include "Scene/ISystem.hpp"

namespace Axiom {
	class AudioUpdateSystem : public ISystem {
	public:
		void Start(Scene& scene) override;

	private:
		// Pulled out so re-spawn-after-Start could reuse it later. Today only
		// Start() calls this; documenting the contract makes future expansion
		// (e.g. polling for newly-added AudioSourceComponents) cheap.
		void StartPlayOnAwake(Scene& scene);
	};
}

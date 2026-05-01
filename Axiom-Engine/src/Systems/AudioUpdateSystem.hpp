#pragma once
#include "Scene/ISystem.hpp"

namespace Axiom {
	class AudioUpdateSystem : public ISystem {
	public:
		void Start(Scene& scene) override;
	};
}

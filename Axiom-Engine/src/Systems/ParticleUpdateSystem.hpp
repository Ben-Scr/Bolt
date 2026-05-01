#pragma once
#include "Scene/ISystem.hpp"

namespace Axiom {
	class ParticleUpdateSystem : public ISystem {
	public:
		virtual void Awake(Scene& scene);
		virtual void Update(Scene& scene);
	};
}
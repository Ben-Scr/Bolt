#pragma once
#include "Core/Layer.hpp"
#include "Core/Export.hpp"

namespace Axiom {
	class AXIOM_API GizmosDebugSystem : public Layer {
	public:
		using Layer::Layer;

		void OnUpdate(Application& app, float dt) override;
	};
}

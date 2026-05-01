#pragma once
#include "Core/Export.hpp"

namespace Axiom {
	class Scene;

	class AXIOM_API ISystem {
	public:
		friend class Scene;
		friend class SceneDefinition;

		virtual ~ISystem() = default;

		// Info: Gets called every frame
		virtual void Update(Scene&) {}

		// Info: Gets called every fixed frame
		virtual void FixedUpdate(Scene&) {}

		// Info: Gets called when scene is created
		virtual void Awake(Scene&) {}

		// Info: Gets called when scene is created directly after awake
		virtual void Start(Scene&) {}

		// Info: Gets called when system is enabled
		virtual void OnEnable(Scene&) {}

		// Info: Gets called when system is disabled
		virtual void OnDisable(Scene&) {}

		// Info: Gets called when system is destroyed
		virtual void OnDestroy(Scene&) {}

		// Info: Gets called on imgui frame start (every frame)
		virtual void OnGui(Scene&) {}

		bool IsEnabled() const { return m_Enabled; }

	private:
		void SetEnabled(bool enabled, Scene& scene) {
			if (m_Enabled == enabled) {
				return;
			}
			m_Enabled = enabled;
			if (m_Enabled) {
				OnEnable(scene);
			}
			else {
				OnDisable(scene);
			}
		}

		bool m_Enabled = true;
	};
}

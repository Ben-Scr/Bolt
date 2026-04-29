#include <pch.hpp>
#include "Editor/EditorComponentRegistration.hpp"

#include "Gui/ComponentInspectors.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptComponentInspector.hpp"

#include "Components/Components.hpp"

namespace Bolt {
	namespace {
		template<typename T>
		void RegisterComponent(SceneManager& sceneManager, const std::string& displayName,
			void (*inspector)(Entity),
			ComponentCategory category = ComponentCategory::Component,
			const std::string& subcategory = "")
		{
			ComponentInfo info{ displayName, subcategory, category };
			info.drawInspector = inspector;
			sceneManager.RegisterComponentType<T>(info);
		}
	}

	void RegisterEditorComponentInspectors(SceneManager& sceneManager) {
		RegisterComponent<NameComponent>(sceneManager, "Name", DrawNameComponentInspector, ComponentCategory::Component, "General");
		RegisterComponent<Transform2DComponent>(sceneManager, "Transform 2D", DrawTransform2DInspector, ComponentCategory::Component, "General");

		RegisterComponent<SpriteRendererComponent>(sceneManager, "Sprite Renderer", DrawSpriteRendererInspector, ComponentCategory::Component, "Rendering");
		RegisterComponent<Camera2DComponent>(sceneManager, "Camera 2D", DrawCamera2DInspector, ComponentCategory::Component, "Rendering");
		RegisterComponent<ParticleSystem2DComponent>(sceneManager, "Particle System 2D", DrawParticleSystem2DInspector, ComponentCategory::Component, "Rendering");

		RegisterComponent<BoxCollider2DComponent>(sceneManager, "Box Collider 2D", DrawBoxCollider2DInspector, ComponentCategory::Component, "Physics");
		RegisterComponent<Rigidbody2DComponent>(sceneManager, "Rigidbody 2D", DrawRigidbody2DInspector, ComponentCategory::Component, "Physics");
		RegisterComponent<FastBody2DComponent>(sceneManager, "Fast Body 2D", DrawFastBody2DInspector, ComponentCategory::Component, "Physics");
		RegisterComponent<FastBoxCollider2DComponent>(sceneManager, "Fast Box Collider 2D", DrawFastBoxCollider2DInspector, ComponentCategory::Component, "Physics");
		RegisterComponent<FastCircleCollider2DComponent>(sceneManager, "Fast Circle Collider 2D", DrawFastCircleCollider2DInspector, ComponentCategory::Component, "Physics");

		RegisterComponent<AudioSourceComponent>(sceneManager, "Audio Source", DrawAudioSourceInspector, ComponentCategory::Component, "Audio");
		RegisterComponent<ScriptComponent>(sceneManager, "Scripts", DrawScriptComponentInspector, ComponentCategory::Component, "Scripting");
	}
}

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
			const std::string& subcategory = "",
			const std::string& serializedName = "")
		{
			ComponentInfo info{ displayName, subcategory, category };
			info.serializedName = serializedName;
			info.drawInspector = inspector;
			sceneManager.RegisterComponentType<T>(info);
		}
	}

	void RegisterEditorComponentInspectors(SceneManager& sceneManager) {
		RegisterComponent<NameComponent>(sceneManager, "Name", DrawNameComponentInspector, ComponentCategory::Component, "General", "name");
		RegisterComponent<Transform2DComponent>(sceneManager, "Transform 2D", DrawTransform2DInspector, ComponentCategory::Component, "General", "Transform2D");

		RegisterComponent<SpriteRendererComponent>(sceneManager, "Sprite Renderer", DrawSpriteRendererInspector, ComponentCategory::Component, "Rendering", "SpriteRenderer");
		RegisterComponent<Camera2DComponent>(sceneManager, "Camera 2D", DrawCamera2DInspector, ComponentCategory::Component, "Rendering", "Camera2D");
		RegisterComponent<ParticleSystem2DComponent>(sceneManager, "Particle System 2D", DrawParticleSystem2DInspector, ComponentCategory::Component, "Rendering", "ParticleSystem2D");

		RegisterComponent<BoxCollider2DComponent>(sceneManager, "Box Collider 2D", DrawBoxCollider2DInspector, ComponentCategory::Component, "Physics", "BoxCollider2D");
		RegisterComponent<Rigidbody2DComponent>(sceneManager, "Rigidbody 2D", DrawRigidbody2DInspector, ComponentCategory::Component, "Physics", "Rigidbody2D");
		RegisterComponent<FastBody2DComponent>(sceneManager, "Fast Body 2D", DrawFastBody2DInspector, ComponentCategory::Component, "Physics", "FastBody2D");
		RegisterComponent<FastBoxCollider2DComponent>(sceneManager, "Fast Box Collider 2D", DrawFastBoxCollider2DInspector, ComponentCategory::Component, "Physics", "FastBoxCollider2D");
		RegisterComponent<FastCircleCollider2DComponent>(sceneManager, "Fast Circle Collider 2D", DrawFastCircleCollider2DInspector, ComponentCategory::Component, "Physics", "FastCircleCollider2D");

		RegisterComponent<AudioSourceComponent>(sceneManager, "Audio Source", DrawAudioSourceInspector, ComponentCategory::Component, "Audio", "AudioSource");
		RegisterComponent<ScriptComponent>(sceneManager, "Scripts", DrawScriptComponentInspector, ComponentCategory::Component, "Scripting", "Scripts");
	}
}

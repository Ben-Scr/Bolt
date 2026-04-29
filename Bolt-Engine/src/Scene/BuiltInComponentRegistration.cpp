#include "pch.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"

#include "Scene/SceneManager.hpp"
#include "Components/Components.hpp"
#include "Scripting/ScriptComponent.hpp"

namespace Bolt {
	namespace {
		template<typename T>
		void RegisterComponent(SceneManager& sceneManager, const std::string& displayName,
			ComponentCategory category = ComponentCategory::Component,
			const std::string& subcategory = "")
		{
			ComponentInfo info{ displayName, subcategory, category };
			sceneManager.RegisterComponentType<T>(info);
		}
	}

	void RegisterBuiltInComponents(SceneManager& sceneManager) {
		// General
		RegisterComponent<Transform2DComponent>(sceneManager, "Transform 2D", ComponentCategory::Component, "General");
		RegisterComponent<RectTransformComponent>(sceneManager, "Rect Transform", ComponentCategory::Component, "General");
		RegisterComponent<NameComponent>(sceneManager, "Name", ComponentCategory::Component, "General");
		RegisterComponent<UUIDComponent>(sceneManager, "UUID", ComponentCategory::Tag);
		RegisterComponent<EntityMetaDataComponent>(sceneManager, "Entity Metadata", ComponentCategory::Tag);
		RegisterComponent<PrefabInstanceComponent>(sceneManager, "Prefab Instance", ComponentCategory::Tag);

		// Rendering
		RegisterComponent<SpriteRendererComponent>(sceneManager, "Sprite Renderer", ComponentCategory::Component, "Rendering");
		RegisterComponent<ImageComponent>(sceneManager, "Image", ComponentCategory::Component, "Rendering");
		RegisterComponent<Camera2DComponent>(sceneManager, "Camera 2D", ComponentCategory::Component, "Rendering");
		RegisterComponent<ParticleSystem2DComponent>(sceneManager, "Particle System 2D", ComponentCategory::Component, "Rendering");

		// Physics
		RegisterComponent<BoxCollider2DComponent>(sceneManager, "Box Collider 2D", ComponentCategory::Component, "Physics");
		RegisterComponent<Rigidbody2DComponent>(sceneManager, "Rigidbody 2D", ComponentCategory::Component, "Physics");

		// Bolt-Physics components (lightweight AABB physics)
		RegisterComponent<FastBody2DComponent>(sceneManager, "Fast Body 2D", ComponentCategory::Component, "Physics");
		RegisterComponent<FastBoxCollider2DComponent>(sceneManager, "Fast Box Collider 2D", ComponentCategory::Component, "Physics");
		RegisterComponent<FastCircleCollider2DComponent>(sceneManager, "Fast Circle Collider 2D", ComponentCategory::Component, "Physics");

		// Audio
		RegisterComponent<AudioSourceComponent>(sceneManager, "Audio Source", ComponentCategory::Component, "Audio");

		//RegisterComponent<ScriptComponent>(sceneManager, "Scripts", ComponentCategory::Component, "Scripts");

		// Tags (not user-addable)
		RegisterComponent<IdTag>(sceneManager, "Id", ComponentCategory::Tag);
		RegisterComponent<StaticTag>(sceneManager, "Static", ComponentCategory::Tag);
		RegisterComponent<DisabledTag>(sceneManager, "Disabled", ComponentCategory::Tag);
		RegisterComponent<DeadlyTag>(sceneManager, "Deadly", ComponentCategory::Tag);
	}
}

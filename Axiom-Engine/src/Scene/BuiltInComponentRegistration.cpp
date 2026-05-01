#include "pch.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"

#include "Scene/SceneManager.hpp"
#include "Components/Components.hpp"
#include "Scripting/ScriptComponent.hpp"

namespace Axiom {
	namespace {
		template<typename T>
		void RegisterComponent(SceneManager& sceneManager, const std::string& displayName,
			ComponentCategory category = ComponentCategory::Component,
			const std::string& subcategory = "",
			const std::string& serializedName = "")
		{
			ComponentInfo info{ displayName, subcategory, category };
			info.serializedName = serializedName;
			sceneManager.RegisterComponentType<T>(info);
		}
	}

	void RegisterBuiltInComponents(SceneManager& sceneManager) {
		// General
		RegisterComponent<Transform2DComponent>(sceneManager, "Transform 2D", ComponentCategory::Component, "General", "Transform2D");
		RegisterComponent<RectTransformComponent>(sceneManager, "Rect Transform", ComponentCategory::Component, "General", "RectTransform");
		RegisterComponent<NameComponent>(sceneManager, "Name", ComponentCategory::Component, "General", "name");
		RegisterComponent<UUIDComponent>(sceneManager, "UUID", ComponentCategory::Tag);
		RegisterComponent<EntityMetaDataComponent>(sceneManager, "Entity Metadata", ComponentCategory::Tag);
		RegisterComponent<PrefabInstanceComponent>(sceneManager, "Prefab Instance", ComponentCategory::Tag);

		// Rendering
		RegisterComponent<SpriteRendererComponent>(sceneManager, "Sprite Renderer", ComponentCategory::Component, "Rendering", "SpriteRenderer");
		RegisterComponent<ImageComponent>(sceneManager, "Image", ComponentCategory::Component, "Rendering", "Image");
		RegisterComponent<Camera2DComponent>(sceneManager, "Camera 2D", ComponentCategory::Component, "Rendering", "Camera2D");
		RegisterComponent<ParticleSystem2DComponent>(sceneManager, "Particle System 2D", ComponentCategory::Component, "Rendering", "ParticleSystem2D");

		// Physics
		RegisterComponent<BoxCollider2DComponent>(sceneManager, "Box Collider 2D", ComponentCategory::Component, "Physics", "BoxCollider2D");
		RegisterComponent<Rigidbody2DComponent>(sceneManager, "Rigidbody 2D", ComponentCategory::Component, "Physics", "Rigidbody2D");

		// Axiom-Physics components (lightweight AABB physics)
		RegisterComponent<FastBody2DComponent>(sceneManager, "Fast Body 2D", ComponentCategory::Component, "Physics", "FastBody2D");
		RegisterComponent<FastBoxCollider2DComponent>(sceneManager, "Fast Box Collider 2D", ComponentCategory::Component, "Physics", "FastBoxCollider2D");
		RegisterComponent<FastCircleCollider2DComponent>(sceneManager, "Fast Circle Collider 2D", ComponentCategory::Component, "Physics", "FastCircleCollider2D");

		// Audio
		RegisterComponent<AudioSourceComponent>(sceneManager, "Audio Source", ComponentCategory::Component, "Audio", "AudioSource");

		//RegisterComponent<ScriptComponent>(sceneManager, "Scripts", ComponentCategory::Component, "Scripts");

		// Tags (not user-addable)
		RegisterComponent<IdTag>(sceneManager, "Id", ComponentCategory::Tag);
		RegisterComponent<StaticTag>(sceneManager, "Static", ComponentCategory::Tag);
		RegisterComponent<DisabledTag>(sceneManager, "Disabled", ComponentCategory::Tag);
		RegisterComponent<DeadlyTag>(sceneManager, "Deadly", ComponentCategory::Tag);
	}
}

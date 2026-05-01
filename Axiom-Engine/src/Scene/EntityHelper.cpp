#include "pch.hpp"
#include "EntityHelper.hpp"

#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/General/RectTransformComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/Tags.hpp"

namespace Axiom {
	void EntityHelper::SetEnabled(Entity entity, bool enabled) {
		if (!enabled && !entity.HasComponent<DisabledTag>())
			entity.AddComponent<DisabledTag>();
		else if (enabled && entity.HasComponent<DisabledTag>())
			entity.RemoveComponent<DisabledTag>();
	}
	bool EntityHelper::IsEnabled(Entity entity) {
		return !entity.HasComponent<DisabledTag>();
	}

	std::size_t EntityHelper::EntitiesCount() {
		std::size_t count = 0;

		SceneManager::Get().ForeachLoadedScene([&](const Scene& scene) {
			count += scene.GetRegistry().view<EntityHandle>().size();
			});

		return count;
	}

	Entity EntityHelper::CreateCamera2DEntity(Scene& scene) {
		Entity entity = CreateWith<Transform2DComponent, Camera2DComponent>(scene);
		entity.AddComponent<NameComponent>(NameComponent("Camera 2D"));
		return entity;
	}

	Entity EntityHelper::CreateCamera2DEntity() {
		Entity entity = CreateWith<Transform2DComponent, Camera2DComponent>();
		entity.AddComponent<NameComponent>(NameComponent("Camera 2D"));
		return entity;
	}

	Entity EntityHelper::CreateSpriteEntity(Scene& scene) {
		return CreateWith<Transform2DComponent, SpriteRendererComponent>(scene);
	}

	Entity EntityHelper::CreateSpriteEntity() {
		return CreateWith<Transform2DComponent, SpriteRendererComponent >();
	}

	Entity EntityHelper::CreateImageEntity(Scene& scene) {
		return CreateWith<RectTransformComponent, ImageComponent>(scene);
	}

	Entity EntityHelper::CreateImageEntity() {
		return CreateWith<RectTransformComponent, ImageComponent>();
	}
}

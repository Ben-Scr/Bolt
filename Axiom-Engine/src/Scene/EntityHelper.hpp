#pragma once

#include "Scene/EntityHandle.hpp"
#include "Scene/Entity.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Scene.hpp"
#include "Components/ComponentUtils.hpp"
#include "Core/Export.hpp"
#include "Core/Log.hpp"

namespace Axiom {
    class AXIOM_API EntityHelper {
    public:
        template<typename... Components>
        static Entity CreateWith(Scene& scene) {
            Entity entity = scene.GetEntity(scene.CreateEntityHandle());

            (entity.AddComponent<Components>(), ...);

            return entity;
        }

        // Info: Creates an entity with the entered Components
        template<typename... Components>
        static Entity CreateWith() {
            Scene* activeScene = SceneManager::Get().GetActiveScene();
            if (!activeScene || !activeScene->IsLoaded()) {
                AIM_ERROR_TAG("EntityHelper", "Cannot create entity because there is no active scene loaded");
                return Entity::Null;
            }

            Entity entity = activeScene->GetEntity(activeScene->CreateEntityHandle());

            (entity.AddComponent<Components>(), ...);

            return entity;
        }

        template<typename... Components>
        static EntityHandle CreateHandleWith(Scene& scene) {
            EntityHandle entity = scene.CreateEntityHandle();

            (scene.AddComponent<Components>(entity), ...);
            return entity;
        }

        template<typename... Components>
        static EntityHandle CreateHandleWith() {
            Scene* activeScene = SceneManager::Get().GetActiveScene();
            if (!activeScene || !activeScene->IsLoaded()) {
                AIM_ERROR_TAG("EntityHelper", "Cannot create entity handle because there is no active scene loaded");
                return entt::null;
            }

            EntityHandle entity = activeScene->CreateEntityHandle();

            (activeScene->AddComponent<Components>(entity), ...);
            return entity;
        }

        static void SetEnabled(Entity entity, bool enabled);
        static bool IsEnabled(Entity entity);

        // Info: Gives you back the global entities count
        static std::size_t EntitiesCount();

        static Entity CreateCamera2DEntity(Scene& scene);
        // Info: Basically calls CreateWith<Transform2D, Canera2D>();
        static Entity CreateCamera2DEntity();
        static Entity CreateSpriteEntity(Scene& scene);
        // Info: Basically calls CreateWith<Transform2D, SpriteRenderer>();
        static Entity CreateSpriteEntity();
        static Entity CreateImageEntity(Scene& scene);
        // Info: Basically calls CreateWith<RectTransform, Image>();
        static Entity CreateImageEntity();
    };

}

#pragma once
#include "pch.hpp"
#include "ParticleUpdateSystem.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Scene.hpp"

#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"

#include <entt/entt.hpp>

namespace Bolt {
	void ParticleUpdateSystem::Awake(Scene& scene) {
		if (!Application::GetIsPlaying()) {
			return;
		}

		for (const auto& [ent, particleSystem] : scene.GetRegistry().view<ParticleSystem2DComponent>(entt::exclude<DisabledTag>).each())
			if (particleSystem.PlayOnAwake)
				particleSystem.Play();
	}

	void ParticleUpdateSystem::Update(Scene& scene) {
		for (const auto& [ent, particleSystem] : scene.GetRegistry().view<ParticleSystem2DComponent>(entt::exclude<DisabledTag>).each())
			particleSystem.Update();
	}
}

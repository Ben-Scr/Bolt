#include "pch.hpp"
#include "Systems/AudioUpdateSystem.hpp"
#include "Scene/Scene.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"

namespace Axiom {
	void AudioUpdateSystem::Start(Scene& scene) {
		if (!Application::GetIsPlaying()) return;

		auto view = scene.GetRegistry().view<AudioSourceComponent>(entt::exclude<DisabledTag>);
		for (auto [entity, audio] : view.each()) {
			if (audio.GetPlayOnAwake() && audio.GetAudioHandle().IsValid()) {
				audio.Play();
			}
		}
	}
}

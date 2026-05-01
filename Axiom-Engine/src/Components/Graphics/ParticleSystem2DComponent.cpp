#include "pch.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"

#include "Math/Random.hpp"
#include "Math/Math.hpp"
#include "Math/VectorMath.hpp"
#include "Scene/Scene.hpp"

#include <Core/Time.hpp>
#include <Core/Application.hpp>

namespace Axiom {
	Transform2DComponent& ParticleSystem2DComponent::GetTransform2D() {
		Transform2DComponent* transform = const_cast<Transform2DComponent*>(TryGetEmitterTransform());
		AIM_ASSERT(transform != nullptr, AxiomErrorCode::InvalidHandle, "Particle system emitter transform is no longer available");
		return *transform;
	}

	const Transform2DComponent& ParticleSystem2DComponent::GetTransform2D() const {
		const Transform2DComponent* transform = TryGetEmitterTransform();
		AIM_ASSERT(transform != nullptr, AxiomErrorCode::InvalidHandle, "Particle system emitter transform is no longer available");
		return *transform;
	}

	const Transform2DComponent* ParticleSystem2DComponent::TryGetEmitterTransform() const
	{
		if (!m_EmitterScene || m_EmitterEntity == entt::null || !m_EmitterScene->IsValid(m_EmitterEntity)) {
			return nullptr;
		}

		if (!m_EmitterScene->HasComponent<Transform2DComponent>(m_EmitterEntity)) {
			return nullptr;
		}

		return &m_EmitterScene->GetComponent<Transform2DComponent>(m_EmitterEntity);
	}

	void ParticleSystem2DComponent::Update() {
		float deltaTime = Application::GetInstance()->GetTime().GetDeltaTime();
		if (deltaTime == 0.f) return;

		if (m_IsEmitting) {
			float toEmit = EmissionSettings.EmitOverTime * deltaTime + m_EmitAccumulator;
			int emitCount = static_cast<int>(toEmit);
			m_EmitAccumulator = toEmit - emitCount;
			Emit(emitCount);

			for (auto& burst : m_Bursts) {
				burst.TimeUntilNext += deltaTime;
				if (burst.TimeUntilNext >= burst.Interval) {
					Emit(burst.Count);
					burst.TimeUntilNext = 0;
				}
			}
		}
		if (m_IsSimulating) {
			for (auto& particle : m_Particles) {
				if (ParticleSettings.UseGravity) {
					particle.Velocity += ParticleSettings.Gravity * deltaTime;
				}

				particle.Transform.Position += particle.Velocity * deltaTime;
			}

			m_Particles.erase(std::remove_if(m_Particles.begin(), m_Particles.end(),
				[deltaTime](Particle& p) {
					return (p.LifeTime -= deltaTime) <= 0.f;
				}), m_Particles.end());
		}
	}

	void ParticleSystem2DComponent::Emit(size_t count) {
		if(!m_IsEmitting || count == 0)
			return;

		const uint32_t maxParticles = RenderingSettings.MaxParticles;

		if (m_Particles.size() >= maxParticles)
			return;

		m_Particles.reserve(Clamp<uint32_t>(m_Particles.size() + count, 0, maxParticles));

		while (count > 0) {
			Particle particle;
			particle.LifeTime = ParticleSettings.LifeTime;

			Vec2 position{ 0 };
			Vec2 scale{ ParticleSettings.Scale, ParticleSettings.Scale };
			float rot{ 0.f };

			std::visit([&](auto const& s) {
				using T = std::decay_t<decltype(s)>;
				if constexpr (std::is_same_v<T, CircleParams>) {
					position = s.IsOnCircle ? RandomOnCircle(s.Radius) : RandomInCircle(s.Radius);
				}
				else if constexpr (std::is_same_v<T, SquareParams>) {
					position = Vec2(Random::NextFloat(-s.HalfExtends.x, s.HalfExtends.x), Random::NextFloat(-s.HalfExtends.y, s.HalfExtends.y));
				}
				}, Shape);

			if (EmissionSettings.EmissionSpace == Space::World) {
				if (const Transform2DComponent* emitterTransform = TryGetEmitterTransform()) {
					position = emitterTransform->TransformPoint(position);
				}
			}

			particle.Velocity = ParticleSettings.MoveDirection * ParticleSettings.Speed;
			particle.Transform.Position = position;
			particle.Transform.Rotation = rot;
			particle.Transform.Scale = scale;
			particle.Color = ParticleSettings.UseRandomColors ? Axiom::Color(Random::NextFloat(0.f, 1.f), Random::NextFloat(0.f, 1.f), Random::NextFloat(0.f, 1.f)) : RenderingSettings.Color;
			m_Particles.push_back(particle);

			if (m_Particles.size() >= maxParticles)
				break;

			count--;
		}
	}
}

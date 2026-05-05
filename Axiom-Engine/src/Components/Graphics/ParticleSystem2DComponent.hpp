#pragma once
#include "Components/General/Transform2DComponent.hpp"
#include "Core/Export.hpp"
#include "Core/UUID.hpp"
#include "Graphics/TextureHandle.hpp"
#include "Collections/Color.hpp"
#include "Scene/EntityHandle.hpp"
#include <variant>
#include <span>

namespace Axiom {
	class Scene;

	class AXIOM_API ParticleSystem2DComponent {
		friend class Scene;
		friend class Renderer2D;
		friend class ParticleUpdateSystem;

	public:

		struct Particle {
			Transform2DComponent Transform;
			Color Color;
			float LifeTime;
			Vec2 Velocity;
		};

		enum class Space {
			Local,
			World
		};

		struct Burst {
			uint32_t Count{ 10 };
			float Interval{ 1.f };
			float TimeUntilNext{ 0.f };
		};

		struct RenderingSettings {
			uint32_t MaxParticles{ 1000 };
			Color Color{ Color::White() };
			short SortingOrder{ 0 };
			uint8_t SortingLayer{ 0 };
		};

		struct ParticleSettings {
			float LifeTime{ 1.f };
			float Speed{ 5.f };
			Vec2 Gravity{ 0.f };
			bool UseGravity{ false };
			bool UseRandomColors{ false };
			float Scale{ 1.f };
			Vec2 MoveDirection{ 0.f, 0.f };
		};

		struct EmissionSettings {
			uint16_t EmitOverTime{ 10 };
			uint16_t RateOverDistance{ 0 };
			Space EmissionSpace{ Space::World };
		};



		struct CircleParams { float Radius = 1.f; bool IsOnCircle = false; };
		struct SquareParams { Vec2 HalfExtends{ 1.f,1.f }; };

		struct RandomColorParams { Color From{ 1.f,1.f,1.f,1.f }; Color To{ 1.f,1.f,1.f,0.f }; };
		struct RandomScaleParams { Vec2 From{ 1.f,1.f }; Vec2 To{ 1.f,1.f }; };
		struct RandomRotationParams { float From{ 0.f }; float To{ 0.f }; };

		enum class ShapeType {
			Circle,
			Square
		};
		using ShapeParams = std::variant<CircleParams, SquareParams>;


		ParticleSystem2DComponent() = default;
		void SetTexture(const TextureHandle& texture, UUID assetId = UUID(0)) { m_TextureHandle = texture; m_TextureAssetId = assetId; }
		const TextureHandle& GetTextureHandle() const { return m_TextureHandle; }
		UUID GetTextureAssetId() const { return m_TextureAssetId; }
		void Emit(size_t count);
		void AddBurst(const Burst& burst) { m_Bursts.push_back(burst); }
		std::span<const Particle> GetParticles() const noexcept { return m_Particles; }
		bool IsPlaying() const { return m_IsEmitting; }
		Transform2DComponent& GetTransform2D();
		const Transform2DComponent& GetTransform2D() const;

		// Info: Enables both emitting and simulating
		void Play() { m_IsEmitting = true; m_IsSimulating = true; }

		// Info: Disables both emitting and simulating
		void Pause() { m_IsEmitting = false; m_IsSimulating = false; }

		// Info: Disables emitting but keeps simulating
		void Stop() { m_IsEmitting = false; }

		void SetIsSimulating(bool enabled) { m_IsSimulating = enabled; }
		void SetIsEmitting(bool enabled) { m_IsEmitting = enabled; }

		bool IsEmitting() const { return m_IsEmitting; }
		bool IsSimulating() const { return m_IsSimulating; }

		void Clear() { m_Particles.clear(); m_Bursts.clear();  m_EmitAccumulator = 0.f; }

		bool PlayOnAwake{ true };
		ParticleSettings ParticleSettings;
		EmissionSettings EmissionSettings;
		ShapeParams Shape = CircleParams{ 1.f };
		RenderingSettings RenderingSettings;

	private:
		// E20: explicit-dt overload — preferred path, called by ParticleUpdateSystem
		// once per frame. Original parameterless Update() forwards to it using the
		// global Application time as a fallback.
		void Update();
		void Update(float deltaTime);
		const Transform2DComponent* TryGetEmitterTransform() const;
		std::vector<Particle> m_Particles;
		std::vector<Burst> m_Bursts;

		// NOTE: Don't deep-copy m_EmitterScene/m_EmitterEntity — they refer to
		// scene-local runtime state that must be re-initialized via on_construct hook.
		// EnTT requires components to be copyable for snapshot/restore, so we cannot
		// delete the copy-ctor outright; if entity duplication ships, add a custom
		// copy override that re-runs the emitter-binding hook on the destination
		// entity instead of carrying these raw fields across.
		Scene* m_EmitterScene{ nullptr };
		EntityHandle m_EmitterEntity{ entt::null };
		float m_EmitAccumulator{ 0.0f };
		bool m_IsEmitting{ false };
		bool m_IsSimulating{ false };
		TextureHandle m_TextureHandle;
		UUID m_TextureAssetId{ 0 };
	};
}

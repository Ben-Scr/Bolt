#pragma once
#include "Audio/AudioHandle.hpp"
#include "Core/Export.hpp"
#include "Core/UUID.hpp"

namespace Axiom {

	class AXIOM_API AudioSourceComponent {
	public:
		AudioSourceComponent() = default;
		AudioSourceComponent(const AudioHandle& audioHandle);


		void Play();
		void Pause();
		void Stop();
		void Resume();
		void Destroy();


		void SetVolume(float volume);
		void SetPitch(float pitch);
		void SetLoop(bool loop);
		void SetAudioHandle(const AudioHandle& audioHandle, UUID assetId = UUID(0));

		void PlayOneShot();


		float GetVolume() const { return m_Volume; }
		float GetPitch() const { return m_Pitch; }
		bool IsLooping() const { return m_Loop; }
		bool IsPlaying() const;
		bool IsPaused() const;
		bool IsValid() const;

		const AudioHandle& GetAudioHandle() const { return m_AudioHandle; }
		UUID GetAudioAssetId() const { return m_AudioAssetId; }
		void SetAudioAssetId(UUID assetId) { m_AudioAssetId = assetId; }

		uint32_t GetInstanceId() const { return m_InstanceId; }
		void SetInstanceId(uint32_t id) { m_InstanceId = id; }

	private:
		AudioHandle m_AudioHandle;
		UUID m_AudioAssetId{ 0 };
		uint32_t m_InstanceId = 0;


		float m_Volume = 1.0f;
		float m_Pitch = 1.0f;
		bool m_Loop = false;
		bool m_PlayOnAwake = false;
	public:
		bool GetPlayOnAwake() const { return m_PlayOnAwake; }
		void SetPlayOnAwake(bool playOnAwake) { m_PlayOnAwake = playOnAwake; }
	};
}

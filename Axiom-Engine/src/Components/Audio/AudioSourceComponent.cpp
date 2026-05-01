#include "pch.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Audio/AudioManager.hpp"
#include <Math/Math.hpp>


namespace Axiom {

	AudioSourceComponent::AudioSourceComponent(const AudioHandle& audioHandle)
		: m_audioHandle(audioHandle)
	{}

	void AudioSourceComponent::Play() {
		if (!m_audioHandle.IsValid()) {
			AIM_CORE_WARN("[{}] Cannot play invalid audio handle", ErrorCodeToString(AxiomErrorCode::InvalidHandle));
			return;
		}
		AudioManager::PlayAudioSource(*this);
	}

	void AudioSourceComponent::Pause() {
		if (m_instanceId == 0) {
			return;
		}
		AudioManager::PauseAudioSource(*this);
	}

	void AudioSourceComponent::Stop() {
		if (m_instanceId == 0) {
			return;
		}
		AudioManager::StopAudioSource(*this);
	}

	void AudioSourceComponent::Resume() {
		if (m_instanceId == 0) {
			return;
		}
		AudioManager::ResumeAudioSource(*this);
	}

	void AudioSourceComponent::Destroy() {
		if (m_instanceId != 0) {
			AudioManager::DestroySoundInstance(m_instanceId);
			m_instanceId = 0;
		}
	}

	void AudioSourceComponent::SetVolume(float volume) {
		m_Volume = Max(0.0f, volume);

		if (m_instanceId != 0) {
			auto* instance = AudioManager::GetSoundInstance(m_instanceId);
			if (instance && instance->IsValid) {
				float masterVolume = AudioManager::GetMasterVolume();
				ma_sound_set_volume(&instance->Sound, m_Volume * masterVolume);
			}
		}
	}

	void AudioSourceComponent::SetPitch(float pitch) {
		m_Pitch = Max(0.01f, pitch);

		if (m_instanceId != 0) {
			auto* instance = AudioManager::GetSoundInstance(m_instanceId);
			if (instance && instance->IsValid) {
				ma_sound_set_pitch(&instance->Sound, m_Pitch);
			}
		}
	}

	void AudioSourceComponent::SetLoop(bool loop) {
		m_Loop = loop;


		if (m_instanceId != 0) {
			auto* instance = AudioManager::GetSoundInstance(m_instanceId);
			if (instance && instance->IsValid) {
				ma_sound_set_looping(&instance->Sound, m_Loop);
			}
		}
	}

	bool AudioSourceComponent::IsPlaying() const {
		if (m_instanceId == 0) {
			return false;
		}

		auto* instance = AudioManager::GetSoundInstance(m_instanceId);
		if (instance && instance->IsValid) {
			return ma_sound_is_playing(&instance->Sound);
		}

		return false;
	}

	bool AudioSourceComponent::IsPaused() const {
		if (m_instanceId == 0) {
			return false;
		}

		auto* instance = AudioManager::GetSoundInstance(m_instanceId);
		if (instance && instance->IsValid) {
			return !ma_sound_is_playing(&instance->Sound) && ma_sound_at_end(&instance->Sound) == MA_FALSE;
		}

		return false;
	}


	void AudioSourceComponent::SetAudioHandle(const AudioHandle& audioHandle, UUID assetId) {
		if (m_instanceId != 0) {
			Stop();
		}

		m_audioHandle = audioHandle;
		m_AudioAssetId = assetId;
	}

	void AudioSourceComponent::PlayOneShot() {
		if (!m_audioHandle.IsValid()) {
			AIM_CORE_WARN("[{}] AudioSource cannot play one-shot - invalid audio handle", ErrorCodeToString(AxiomErrorCode::InvalidHandle));
			return;
		}
		AudioManager::PlayOneShot(m_audioHandle, m_Volume);
	}

	bool AudioSourceComponent::IsValid() const { return m_audioHandle.IsValid(); }
}

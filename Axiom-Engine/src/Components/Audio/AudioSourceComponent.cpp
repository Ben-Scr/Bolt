#include "pch.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Audio/AudioManager.hpp"
#include <Math/Math.hpp>


namespace Axiom {

	AudioSourceComponent::AudioSourceComponent(const AudioHandle& audioHandle)
		: m_AudioHandle(audioHandle)
	{}

	void AudioSourceComponent::Play() {
		if (!m_AudioHandle.IsValid()) {
			AIM_CORE_WARN("[{}] Cannot play invalid audio handle", ErrorCodeToString(AxiomErrorCode::InvalidHandle));
			return;
		}
		AudioManager::PlayAudioSource(*this);
	}

	void AudioSourceComponent::Pause() {
		if (m_InstanceId == 0) {
			return;
		}
		AudioManager::PauseAudioSource(*this);
	}

	void AudioSourceComponent::Stop() {
		if (m_InstanceId == 0) {
			return;
		}
		AudioManager::StopAudioSource(*this);
	}

	void AudioSourceComponent::Resume() {
		if (m_InstanceId == 0) {
			return;
		}
		AudioManager::ResumeAudioSource(*this);
	}

	void AudioSourceComponent::Destroy() {
		if (m_InstanceId != 0) {
			AudioManager::DestroySoundInstance(m_InstanceId);
			m_InstanceId = 0;
		}
	}

	void AudioSourceComponent::SetVolume(float volume) {
		m_Volume = Max(0.0f, volume);

		if (m_InstanceId != 0) {
			auto* instance = AudioManager::GetSoundInstance(m_InstanceId);
			if (instance && instance->IsValid) {
				ma_sound_set_volume(&instance->Sound, m_Volume);
			}
		}
	}

	void AudioSourceComponent::SetPitch(float pitch) {
		m_Pitch = Max(0.01f, pitch);

		if (m_InstanceId != 0) {
			auto* instance = AudioManager::GetSoundInstance(m_InstanceId);
			if (instance && instance->IsValid) {
				ma_sound_set_pitch(&instance->Sound, m_Pitch);
			}
		}
	}

	void AudioSourceComponent::SetLoop(bool loop) {
		m_Loop = loop;


		if (m_InstanceId != 0) {
			auto* instance = AudioManager::GetSoundInstance(m_InstanceId);
			if (instance && instance->IsValid) {
				ma_sound_set_looping(&instance->Sound, m_Loop);
			}
		}
	}

	bool AudioSourceComponent::IsPlaying() const {
		if (m_InstanceId == 0) {
			return false;
		}

		auto* instance = AudioManager::GetSoundInstance(m_InstanceId);
		if (instance && instance->IsValid) {
			return ma_sound_is_playing(&instance->Sound);
		}

		return false;
	}

	bool AudioSourceComponent::IsPaused() const {
		if (m_InstanceId == 0) {
			return false;
		}

		auto* instance = AudioManager::GetSoundInstance(m_InstanceId);
		if (instance && instance->IsValid) {
			// A never-started sound is not playing AND not at end either — exclude it
			// by requiring that some playback cursor has progressed (at_end is set
			// after a non-looping sound finishes, but only if it ever started).
			if (ma_sound_is_playing(&instance->Sound)) return false;
			if (ma_sound_at_end(&instance->Sound) == MA_TRUE) return false;
			return ma_sound_get_time_in_pcm_frames(&instance->Sound) > 0;
		}

		return false;
	}


	void AudioSourceComponent::SetAudioHandle(const AudioHandle& audioHandle, UUID assetId) {
		if (m_InstanceId != 0) {
			Destroy();
		}

		m_AudioHandle = audioHandle;
		m_AudioAssetId = assetId;
	}

	void AudioSourceComponent::PlayOneShot() {
		if (!m_AudioHandle.IsValid()) {
			AIM_CORE_WARN("[{}] AudioSource cannot play one-shot - invalid audio handle", ErrorCodeToString(AxiomErrorCode::InvalidHandle));
			return;
		}
		AudioManager::PlayOneShot(m_AudioHandle, m_Volume);
	}

	bool AudioSourceComponent::IsValid() const { return m_AudioHandle.IsValid(); }
}

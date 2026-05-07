#pragma once

#include "AudioHandle.hpp"
#include "Core/Export.hpp"

#include <miniaudio.h>
#include <glm/vec3.hpp>

#include <chrono>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace Axiom {
	class AudioSourceComponent;
	class Audio;

	class AXIOM_API AudioManager {
	public:
		static constexpr uint32_t MAX_CONCURRENT_SOUNDS = 64;
		static constexpr uint32_t MAX_SOUNDS_PER_FRAME = 8;
		static constexpr float MIN_SOUND_INTERVAL = 0.1f;

		static bool Initialize();
		static void Shutdown();
		static void Update();


		static AudioHandle LoadAudio(const std::string_view& path);
		static AudioHandle LoadAudioByUUID(uint64_t assetId);
		static void UnloadAudio(const AudioHandle& audioHandle);
		static void UnloadAllAudio();

		/// Frees Audio entries not referenced by any Scene component. Returns the number of entries freed.
		static size_t PurgeUnreferenced();


		static void PlayAudioSource(AudioSourceComponent& source);
		static void PauseAudioSource(AudioSourceComponent& source);
		static void StopAudioSource(AudioSourceComponent& source);
		static void ResumeAudioSource(AudioSourceComponent& source);


		static void SetMasterVolume(float volume);
		static float GetMasterVolume() { return s_masterVolume; }


		static void PlayOneShot(const AudioHandle& audioHandle, float volume = 1.0f);

		static void SetMaxConcurrentSounds(uint32_t maxSounds);
		static void SetMaxSoundsPerFrame(uint32_t maxPerFrame);
		static uint32_t GetActiveSoundCount();


		static bool IsInitialized() { return s_IsInitialized; }
		static bool IsAudioLoaded(const AudioHandle& audioHandle);
		static const Audio* GetAudio(const AudioHandle& audioHandle);
		static std::string GetAudioName(const AudioHandle& audioHandle);
		static uint64_t GetAudioAssetUUID(const AudioHandle& audioHandle);


		struct SoundInstance {
			ma_resource_manager_data_source DataSource{};
			ma_sound Sound{};
			AudioHandle AudioHandle;
			// Bumped on every Recycle so a stale handle to this slot fails GetSoundInstance.
			// Without this, a recycled index silently aliases a different sound (the bug that
			// produced the audit's CR2).
			uint16_t Generation = 0;
			bool HasDataSource = false;
			bool IsValid = false;
		};

		static SoundInstance* GetSoundInstance(uint32_t instanceId);

	private:
		AudioManager() = delete;
		~AudioManager() = delete;
		AudioManager(const AudioManager&) = delete;
		AudioManager& operator=(const AudioManager&) = delete;


		static ma_engine s_Engine;
		static bool s_IsInitialized;

		struct SoundRequest {
			AudioHandle Handle;
			float Volume;
			float Priority;
			glm::vec3 Position;
			std::chrono::steady_clock::time_point RequestTime;

			bool operator<(const SoundRequest& other) const {
				return Priority < other.Priority;
			}
		};

		struct SoundLimitData {
			std::chrono::steady_clock::time_point LastPlayTime;
			uint32_t FramePlayCount;
		};


		static uint32_t s_maxConcurrentSounds;
		static uint32_t s_maxSoundsPerFrame;
		static uint32_t s_soundsPlayedThisFrame;
		static uint32_t s_activeSoundCount;


		static std::priority_queue<SoundRequest> s_soundQueue;
		static std::unordered_map<AudioHandle::HandleType, SoundLimitData> s_soundLimits;


		static bool CanPlaySound(const AudioHandle& audioHandle, float priority);
		static void ProcessSoundQueue();
		static void ThrottleSound(const AudioHandle& audioHandle);
		static bool IsThrottled(const AudioHandle& audioHandle);


		static std::unordered_map<AudioHandle::HandleType, std::unique_ptr<Audio>> s_audioMap;
		static std::unordered_map<std::string, AudioHandle::HandleType> s_audioPathToHandle;
		static AudioHandle::HandleType s_nextHandle;

		// Held via unique_ptr so the slot's address is stable while miniaudio's
		// audio thread reads from `&Sound` concurrently. A bare `vector<SoundInstance>`
		// would relocate `ma_sound` on growth and crash mid-playback. Free slots
		// hold null until reused.
		static std::vector<std::unique_ptr<SoundInstance>> s_soundInstances;
		static std::vector<uint32_t> s_freeInstanceIndices;


		static float s_masterVolume;
		static std::string s_RootPath;


		static AudioHandle::HandleType GenerateHandle();
		static AudioHandle FindAudioByPath(const std::string& path);
		static bool RegisterAudioData(const Audio& audio);
		static void UnregisterAudioData(const Audio& audio);
		static uint32_t CreateSoundInstance(const AudioHandle& audioHandle);
		static void DestroySoundInstance(uint32_t instanceId);
		static void RecycleSoundInstance(uint32_t index);
		static void CleanupFinishedSounds();
		static void RecalculateActiveSoundCount();
		static bool StartOneShotInstance(const AudioHandle& audioHandle, float volume);

		static void UpdateListener();
		static void UpdateSoundInstances();

		friend class AudioSourceComponent;
	};
}

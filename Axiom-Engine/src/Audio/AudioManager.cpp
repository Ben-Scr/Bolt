#include "pch.hpp"

#include "Assets/AssetRegistry.hpp"
#include "AudioManager.hpp"
#include "Audio.hpp"
#include  <Math/Common.hpp>

#include "Serialization/Path.hpp"

#include "Components/Audio/AudioSourceComponent.hpp"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <filesystem>

namespace Axiom {
	namespace {
		std::string NormalizeAudioPath(std::filesystem::path path)
		{
			if (path.empty()) {
				return {};
			}

			std::error_code ec;
			if (std::filesystem::exists(path, ec)) {
				std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, ec);
				if (!ec) {
					return canonicalPath.make_preferred().string();
				}
				ec.clear();
			}

			const std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
			if (ec) {
				return path.lexically_normal().make_preferred().string();
			}

			return absolutePath.lexically_normal().make_preferred().string();
		}
	}

	ma_engine AudioManager::s_Engine{};
	bool AudioManager::s_IsInitialized = false;
	std::unordered_map<AudioHandle::HandleType, std::unique_ptr<Audio>> AudioManager::s_audioMap;
	std::unordered_map<std::string, AudioHandle::HandleType> AudioManager::s_audioPathToHandle;
	AudioHandle::HandleType AudioManager::s_nextHandle = 1;
	std::vector<AudioManager::SoundInstance> AudioManager::s_soundInstances;
	std::vector<uint32_t> AudioManager::s_freeInstanceIndices;
	float AudioManager::s_masterVolume = 1.0f;
	std::string AudioManager::s_RootPath = Path::Combine("AxiomAssets", "Audio");

	uint32_t AudioManager::s_maxConcurrentSounds = MAX_CONCURRENT_SOUNDS;
	uint32_t AudioManager::s_maxSoundsPerFrame = MAX_SOUNDS_PER_FRAME;
	uint32_t AudioManager::s_soundsPlayedThisFrame = 0;
	uint32_t AudioManager::s_activeSoundCount = 0;
	std::priority_queue<AudioManager::SoundRequest> AudioManager::s_soundQueue;
	std::unordered_map<AudioHandle::HandleType, AudioManager::SoundLimitData> AudioManager::s_soundLimits;


	bool AudioManager::Initialize() {
		if (s_IsInitialized) {
			AIM_CORE_WARN("AudioManager already initialized");
			return true;
		}

		std::string audioDir = Path::ResolveAxiomAssets("Audio");
		if (audioDir.empty()) {
			AIM_CORE_WARN("AxiomAssets/Audio not found");
			audioDir = Path::Combine(Path::ExecutableDir(), "AxiomAssets", "Audio");
		}
		s_RootPath = audioDir;

		ma_result result = ma_engine_init(nullptr, &s_Engine);
		if (result != MA_SUCCESS) {
			AIM_CORE_ERROR("[{}] Failed to initialize miniaudio engine. Error: {}", ErrorCodeToString(AxiomErrorCode::LoadFailed), static_cast<int>(result));
			return false;
		}

		s_soundInstances.reserve(256);
		s_freeInstanceIndices.reserve(256);

		UpdateListener();

		s_IsInitialized = true;
		return true;
	}

	void AudioManager::Shutdown() {
		if (!s_IsInitialized) {
			AIM_CORE_WARN("AudioManager isn't initialized");
			return;
		}

		UnloadAllAudio();

		s_soundInstances.clear();
		s_freeInstanceIndices.clear();
		s_soundLimits.clear();
		s_activeSoundCount = 0;
		s_soundsPlayedThisFrame = 0;
		s_soundQueue = {};
		s_audioPathToHandle.clear();

		ma_engine_uninit(&s_Engine);
		s_IsInitialized = false;
	}

	void AudioManager::Update() {
		if (!s_IsInitialized) {
			return;
		}

		s_soundsPlayedThisFrame = 0;
		CleanupFinishedSounds();
		RecalculateActiveSoundCount();
		ProcessSoundQueue();
		UpdateListener();
		UpdateSoundInstances();
		RecalculateActiveSoundCount();
	}

	bool AudioManager::CanPlaySound(const AudioHandle& audioHandle, float priority) {
		if (priority >= 2.0f) {
			return true;
		}


		if (s_activeSoundCount >= s_maxConcurrentSounds) {
			return priority > 1.5f;
		}

		if (s_soundsPlayedThisFrame >= s_maxSoundsPerFrame) {
			return priority > 1.8f;
		}

		return true;
	}

	void AudioManager::ProcessSoundQueue() {
		uint32_t processed = 0;
		const uint32_t maxProcessPerFrame = 4;

		while (!s_soundQueue.empty() && processed < maxProcessPerFrame &&
			s_soundsPlayedThisFrame < s_maxSoundsPerFrame &&
			s_activeSoundCount < s_maxConcurrentSounds) {

			SoundRequest request = s_soundQueue.top();
			s_soundQueue.pop();


			auto now = std::chrono::steady_clock::now();
			auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - request.RequestTime);
			if (age.count() > 200) {
				processed++;
				continue;
			}

			if (IsThrottled(request.GetHandle)) {
				processed++;
				continue;
			}

			if (StartOneShotInstance(request.GetHandle, request.Volume)) {
				s_soundsPlayedThisFrame++;
				ThrottleSound(request.GetHandle);
				s_activeSoundCount++;
			}
			processed++;
		}
	}

	void AudioManager::ThrottleSound(const AudioHandle& audioHandle) {
		auto& limitData = s_soundLimits[audioHandle.GetHandle()];
		limitData.LastPlayTime = std::chrono::steady_clock::now();
		limitData.FramePlayCount++;
	}

	bool AudioManager::IsThrottled(const AudioHandle& audioHandle) {
		auto it = s_soundLimits.find(audioHandle.GetHandle());
		if (it == s_soundLimits.end()) {
			return false;
		}

		auto now = std::chrono::steady_clock::now();
		auto timeSinceLastPlay = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - it->second.LastPlayTime);

		return timeSinceLastPlay.count() < (MIN_SOUND_INTERVAL * 1000);
	}

	void AudioManager::SetMaxConcurrentSounds(uint32_t maxSounds) {
		s_maxConcurrentSounds = Min(maxSounds, 128u);
	}

	void AudioManager::SetMaxSoundsPerFrame(uint32_t maxPerFrame) {
		s_maxSoundsPerFrame = Min(maxPerFrame, 16u);
	}

	uint32_t AudioManager::GetActiveSoundCount() {
		return s_activeSoundCount;
	}

	AudioHandle AudioManager::LoadAudio(const std::string_view& path) {
		if (!s_IsInitialized) {
			AIM_CORE_ERROR("[{}] AudioManager not initialized", ErrorCodeToString(AxiomErrorCode::NotInitialized));
			return AudioHandle();
		}

		const std::string requestedPath(path);
		const std::string fullpath = NormalizeAudioPath(std::filesystem::path(requestedPath));
		if (const AudioHandle existing = FindAudioByPath(fullpath); existing.IsValid()) {
			return existing;
		}

		const std::string rootPath = NormalizeAudioPath(std::filesystem::path(Path::Combine(s_RootPath, requestedPath)));
		if (!rootPath.empty() && rootPath != fullpath) {
			if (const AudioHandle existing = FindAudioByPath(rootPath); existing.IsValid()) {
				return existing;
			}
		}

		std::string resolvedPath = fullpath;
		auto audio = std::make_unique<Audio>();
		if (!audio->LoadFromFile(resolvedPath)) {
			resolvedPath = rootPath;
			audio = std::make_unique<Audio>();
			if (resolvedPath.empty() || !audio->LoadFromFile(resolvedPath)) {
				AIM_CORE_ERROR("[{}] AudioManager: Failed to load audio: {}", ErrorCodeToString(AxiomErrorCode::LoadFailed), requestedPath);
				return AudioHandle();
			}
		}

		AudioHandle::HandleType id = GenerateHandle();
		if (!RegisterAudioData(*audio)) {
			AIM_CORE_WARN_TAG("AudioManager", "Falling back to on-demand audio loading for '{}'", resolvedPath);
		}
		s_audioMap[id] = std::move(audio);
		s_audioPathToHandle[resolvedPath] = id;
		return AudioHandle(id);
	}

	AudioHandle AudioManager::LoadAudioByUUID(uint64_t assetId) {
		if (assetId == 0 || !AssetRegistry::IsAudio(assetId)) {
			return AudioHandle();
		}

		const std::string path = AssetRegistry::ResolvePath(assetId);
		if (path.empty()) {
			return AudioHandle();
		}

		return LoadAudio(path);
	}

	void AudioManager::UnloadAudio(const AudioHandle& audioHandle) {
		if (!audioHandle.IsValid()) {
			return;
		}

		auto it = s_audioMap.find(audioHandle.GetHandle());
		if (it != s_audioMap.end()) {
			if (it->second) {
				s_audioPathToHandle.erase(it->second->GetFilepath());
			}

			for (auto& instance : s_soundInstances) {
				if (instance.IsValid && instance.AudioHandle == audioHandle) {
					RecycleSoundInstance(static_cast<uint32_t>(&instance - s_soundInstances.data()));
				}
			}

			UnregisterAudioData(*it->second);
			s_audioMap.erase(it);
			s_soundLimits.erase(audioHandle.GetHandle());
		}
	}

	void AudioManager::UnloadAllAudio() {

		for (auto& instance : s_soundInstances) {
			if (instance.IsValid) {
				RecycleSoundInstance(static_cast<uint32_t>(&instance - s_soundInstances.data()));
			}
		}

		for (const auto& [id, audio] : s_audioMap) {
			if (audio) {
				UnregisterAudioData(*audio);
			}
		}

		s_audioMap.clear();
		s_audioPathToHandle.clear();
		s_nextHandle = 1;
		s_soundLimits.clear();
		s_soundQueue = {};
		s_activeSoundCount = 0;
	}

	void AudioManager::PlayAudioSource(AudioSourceComponent& source) {
		if (!s_IsInitialized) {
			AIM_CORE_WARN("AudioManager not initialized");
			return;
		}
		if (!source.GetAudioHandle().IsValid()) {
			AIM_CORE_WARN("[{}] Invalid AudioHandle", ErrorCodeToString(AxiomErrorCode::InvalidHandle));
			return;
		}

		if (source.GetInstanceId() != 0) {
			StopAudioSource(source);
		}

		uint32_t instanceId = CreateSoundInstance(source.GetAudioHandle());
		if (instanceId == 0) {
			AIM_CORE_ERROR("[{}] Failed to create sound instance", ErrorCodeToString(AxiomErrorCode::LoadFailed));
			return;
		}

		source.SetInstanceId(instanceId);
		SoundInstance* instance = GetSoundInstance(instanceId);

		if (instance) {
			ma_sound_set_volume(&instance->Sound, source.GetVolume() * s_masterVolume);
			ma_sound_set_pitch(&instance->Sound, source.GetPitch());
			ma_sound_set_looping(&instance->Sound, source.IsLooping());
			ma_sound_set_positioning(&instance->Sound, ma_positioning_relative);


			ma_result result = ma_sound_start(&instance->Sound);
			if (result != MA_SUCCESS) {
				AIM_CORE_ERROR("[{}] Failed to start sound playback. Error: {}", ErrorCodeToString(AxiomErrorCode::LoadFailed), static_cast<int>(result));
				source.SetInstanceId(0);
				DestroySoundInstance(instanceId);
			}
		}
		else {
			AIM_CORE_ERROR("[{}] Failed to retrieve sound instance after creation", ErrorCodeToString(AxiomErrorCode::NullReference));
			source.SetInstanceId(0);
			DestroySoundInstance(instanceId);
		}
	}

	void AudioManager::PauseAudioSource(AudioSourceComponent& source) {
		if (!s_IsInitialized || source.GetInstanceId() == 0) {
			return;
		}

		SoundInstance* instance = GetSoundInstance(source.GetInstanceId());
		if (instance && instance->IsValid) {
			ma_sound_stop(&instance->Sound);
		}
	}

	void AudioManager::StopAudioSource(AudioSourceComponent& source) {
		if (!s_IsInitialized || source.GetInstanceId() == 0) {
			return;
		}

		DestroySoundInstance(source.GetInstanceId());
		source.SetInstanceId(0);
	}

	void AudioManager::ResumeAudioSource(AudioSourceComponent& source) {
		if (!s_IsInitialized || source.GetInstanceId() == 0) {
			return;
		}

		SoundInstance* instance = GetSoundInstance(source.GetInstanceId());
		if (instance && instance->IsValid) {
			ma_sound_start(&instance->Sound);
		}
	}

	void AudioManager::SetMasterVolume(float volume) {
		s_masterVolume = Max(0.0f, volume);

		if (s_IsInitialized) {
			ma_engine_set_volume(&s_Engine, s_masterVolume);
		}
	}

	void AudioManager::PlayOneShot(const AudioHandle& audioHandle, float volume) {
		if (!s_IsInitialized || !audioHandle.IsValid()) {
			return;
		}

		SoundRequest request{};
		request.GetHandle = audioHandle;
		request.Volume = volume;
		request.Priority = 1.0f;
		request.RequestTime = std::chrono::steady_clock::now();

		if (IsThrottled(audioHandle) || !CanPlaySound(audioHandle, request.Priority)) {
			s_soundQueue.push(request);
			return;
		}

		if (StartOneShotInstance(audioHandle, volume)) {
			s_soundsPlayedThisFrame++;
			ThrottleSound(audioHandle);
		}
	}

	bool AudioManager::RegisterAudioData(const Audio& audio) {
		if (!s_IsInitialized || !audio.IsLoaded() || audio.GetData() == nullptr || audio.GetFrameCount() == 0) {
			return false;
		}

		ma_resource_manager* resourceManager = ma_engine_get_resource_manager(&s_Engine);
		if (!resourceManager) {
			return false;
		}

		const ma_result result = ma_resource_manager_register_decoded_data(
			resourceManager,
			audio.GetFilepath().c_str(),
			audio.GetData(),
			audio.GetFrameCount(),
			audio.GetFormat(),
			audio.GetChannels(),
			audio.GetSampleRate());
		if (result != MA_SUCCESS) {
			AIM_CORE_WARN_TAG("AudioManager", "Failed to register decoded audio data for '{}': {}", audio.GetFilepath(), static_cast<int>(result));
			return false;
		}

		return true;
	}

	void AudioManager::UnregisterAudioData(const Audio& audio) {
		if (!s_IsInitialized || audio.GetFilepath().empty()) {
			return;
		}

		if (ma_resource_manager* resourceManager = ma_engine_get_resource_manager(&s_Engine)) {
			ma_resource_manager_unregister_data(resourceManager, audio.GetFilepath().c_str());
		}
	}

	bool AudioManager::IsAudioLoaded(const AudioHandle& audioHandle) {
		if (!audioHandle.IsValid()) {
			return false;
		}

		return s_audioMap.find(audioHandle.GetHandle()) != s_audioMap.end();
	}

	const Audio* AudioManager::GetAudio(const AudioHandle& audioHandle) {
		if (!audioHandle.IsValid()) {
			return nullptr;
		}

		auto it = s_audioMap.find(audioHandle.GetHandle());
		return (it != s_audioMap.end()) ? it->second.get() : nullptr;
	}

	std::string AudioManager::GetAudioName(const AudioHandle& audioHandle) {
		const Audio* audio = GetAudio(audioHandle);
		if (!audio) return "";
		return audio->GetFilepath();
	}

	uint64_t AudioManager::GetAudioAssetUUID(const AudioHandle& audioHandle) {
		const Audio* audio = GetAudio(audioHandle);
		if (!audio) {
			return 0;
		}

		return AssetRegistry::GetOrCreateAssetUUID(audio->GetFilepath());
	}

	AudioHandle::HandleType AudioManager::GenerateHandle() {
		return s_nextHandle++;
	}

	AudioHandle AudioManager::FindAudioByPath(const std::string& path) {
		auto pathIt = s_audioPathToHandle.find(path);
		if (pathIt == s_audioPathToHandle.end()) {
			return AudioHandle();
		}

		auto audioIt = s_audioMap.find(pathIt->second);
		if (audioIt != s_audioMap.end() && audioIt->second && audioIt->second->GetFilepath() == path) {
			return AudioHandle(pathIt->second);
		}

		s_audioPathToHandle.erase(pathIt);
		return AudioHandle();
	}

	uint32_t AudioManager::CreateSoundInstance(const AudioHandle& audioHandle) {
		if (!audioHandle.IsValid()) {
			return 0;
		}

		const Audio* audio = GetAudio(audioHandle);
		if (!audio || !audio->IsLoaded()) {
			return 0;
		}

		uint32_t instanceId;

		if (!s_freeInstanceIndices.empty()) {
			instanceId = s_freeInstanceIndices.back();
			s_freeInstanceIndices.pop_back();
		}
		else {
			instanceId = static_cast<uint32_t>(s_soundInstances.size());
			s_soundInstances.emplace_back();
		}

		SoundInstance& instance = s_soundInstances[instanceId];
		const ma_uint32 dataSourceFlags = MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE;
		ma_result result = ma_resource_manager_data_source_init(
			ma_engine_get_resource_manager(&s_Engine),
			audio->GetFilepath().c_str(),
			dataSourceFlags,
			nullptr,
			&instance.DataSource);
		if (result != MA_SUCCESS) {
			AIM_CORE_WARN("[{}] AudioManager: Failed to create sound data source. Error: {}", ErrorCodeToString(AxiomErrorCode::LoadFailed), static_cast<int>(result));
			if (instanceId == s_soundInstances.size() - 1) {
				s_soundInstances.pop_back();
			}
			else {
				s_freeInstanceIndices.push_back(instanceId);
			}
			return 0;
		}

		instance.HasDataSource = true;
		result = ma_sound_init_from_data_source(&s_Engine, &instance.DataSource, 0, nullptr, &instance.Sound);
		if (result != MA_SUCCESS) {
			ma_resource_manager_data_source_uninit(&instance.DataSource);
			instance.HasDataSource = false;
			AIM_CORE_WARN("[{}] AudioManager: Failed to create sound instance. Error: {}", ErrorCodeToString(AxiomErrorCode::LoadFailed), static_cast<int>(result));
			if (instanceId == s_soundInstances.size() - 1) {
				s_soundInstances.pop_back();
			}
			else {
				s_freeInstanceIndices.push_back(instanceId);
			}
			return 0;
		}

		instance.AudioHandle = audioHandle;
		instance.IsValid = true;

		return instanceId + 1;
	}

	void AudioManager::DestroySoundInstance(uint32_t instanceId) {
		if (instanceId == 0) {
			return;
		}

		uint32_t index = instanceId - 1;

		if (index >= s_soundInstances.size()) {
			return;
		}

		RecycleSoundInstance(index);
	}

	AudioManager::SoundInstance* AudioManager::GetSoundInstance(uint32_t instanceId) {
		if (instanceId == 0) {
			return nullptr;
		}

		uint32_t index = instanceId - 1;
		if (index >= s_soundInstances.size()) {
			return nullptr;
		}

		SoundInstance& instance = s_soundInstances[index];
		if (!instance.IsValid) {
			return nullptr;
		}

		return  &instance;
	}

	void AudioManager::RecycleSoundInstance(uint32_t index) {
		if (index >= s_soundInstances.size()) {
			return;
		}

		SoundInstance& instance = s_soundInstances[index];
		if (!instance.IsValid) {
			return;
		}

		ma_sound_stop(&instance.Sound);
		ma_sound_uninit(&instance.Sound);
		if (instance.HasDataSource) {
			ma_resource_manager_data_source_uninit(&instance.DataSource);
			instance.HasDataSource = false;
		}
		instance.IsValid = false;
		instance.AudioHandle = AudioHandle();
		s_freeInstanceIndices.push_back(index);
	}

	void AudioManager::CleanupFinishedSounds() {
		for (size_t i = 0; i < s_soundInstances.size(); ++i) {
			SoundInstance& instance = s_soundInstances[i];

			if (instance.IsValid && !ma_sound_is_playing(&instance.Sound) && !ma_sound_is_looping(&instance.Sound)) {
				RecycleSoundInstance(static_cast<uint32_t>(i));
			}
		}
	}

	void AudioManager::UpdateListener() {
		if (!s_IsInitialized) {
			return;
		}
	}

	void AudioManager::UpdateSoundInstances() {
		if (!s_IsInitialized) {
			return;
		}
	}

	void AudioManager::RecalculateActiveSoundCount() {
		s_activeSoundCount = 0;
		for (const auto& instance : s_soundInstances) {
			if (instance.IsValid && ma_sound_is_playing(&instance.Sound)) {
				s_activeSoundCount++;
			}
		}
	}

	bool AudioManager::StartOneShotInstance(const AudioHandle& audioHandle, float volume) {
		const uint32_t instanceId = CreateSoundInstance(audioHandle);
		if (instanceId == 0) {
			AIM_CORE_WARN("[{}] Failed to create one-shot sound instance", ErrorCodeToString(AxiomErrorCode::LoadFailed));
			return false;
		}

		SoundInstance* instance = GetSoundInstance(instanceId);
		if (!instance) {
			AIM_CORE_WARN("[{}] Failed to retrieve one-shot sound instance", ErrorCodeToString(AxiomErrorCode::NullReference));
			DestroySoundInstance(instanceId);
			return false;
		}

		ma_sound_set_volume(&instance->Sound, volume * s_masterVolume);
		const ma_result result = ma_sound_start(&instance->Sound);
		if (result != MA_SUCCESS) {
			AIM_CORE_WARN("[{}] Failed to start one-shot sound. Error: {}", ErrorCodeToString(AxiomErrorCode::LoadFailed), static_cast<int>(result));
			DestroySoundInstance(instanceId);
			return false;
		}

		return true;
	}

}

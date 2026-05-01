#include "pch.hpp"
#include "Audio/Audio.hpp"

#include <limits>

namespace Axiom {

	Audio::~Audio() {
		Cleanup();
	}

	Audio::Audio(Audio&& other) noexcept
		: m_DecodedFrames(std::move(other.m_DecodedFrames))
		, m_Format(other.m_Format)
		, m_Channels(other.m_Channels)
		, m_SampleRate(other.m_SampleRate)
		, m_FrameCount(other.m_FrameCount)
		, m_IsLoaded(other.m_IsLoaded)
		, m_Filepath(std::move(other.m_Filepath))
	{
		other.m_Format = ma_format_unknown;
		other.m_Channels = 0;
		other.m_SampleRate = 0;
		other.m_FrameCount = 0;
		other.m_IsLoaded = false;
		other.m_Filepath.clear();
	}

	Audio& Audio::operator=(Audio&& other) noexcept {
		if (this != &other) {
			Cleanup();

			m_DecodedFrames = std::move(other.m_DecodedFrames);
			m_Format = other.m_Format;
			m_Channels = other.m_Channels;
			m_SampleRate = other.m_SampleRate;
			m_FrameCount = other.m_FrameCount;
			m_IsLoaded = other.m_IsLoaded;
			m_Filepath = std::move(other.m_Filepath);

			other.m_Format = ma_format_unknown;
			other.m_Channels = 0;
			other.m_SampleRate = 0;
			other.m_FrameCount = 0;
			other.m_IsLoaded = false;
			other.m_Filepath.clear();
		}
		return *this;
	}

	bool Audio::LoadFromFile(const std::string& filepath) {
		if (filepath.empty()) {
			AIM_CORE_WARN_TAG("Audio", "Empty audio file path");
			return false;
		}

		Cleanup();

		ma_decoder decoder{};
		ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
		ma_result result = ma_decoder_init_file(filepath.c_str(), &config, &decoder);

		if (result != MA_SUCCESS) {
			AIM_CORE_ERROR_TAG("Audio", "Failed to load audio: {}", filepath);
			return false;
		}

		m_Format = decoder.outputFormat;
		m_Channels = decoder.outputChannels;
		m_SampleRate = decoder.outputSampleRate;

		ma_uint64 frameCount = 0;
		result = ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);
		if (result == MA_SUCCESS && frameCount > 0) {
			const ma_uint64 channelCount = static_cast<ma_uint64>(m_Channels);
			const ma_uint64 sampleCount = frameCount * channelCount;
			const ma_uint64 maxSamples = static_cast<ma_uint64>(std::numeric_limits<size_t>::max() / sizeof(float));
			if (sampleCount > maxSamples) {
				AIM_CORE_ERROR_TAG("Audio", "Audio file is too large to cache in memory: {}", filepath);
				ma_decoder_uninit(&decoder);
				Cleanup();
				return false;
			}

			m_DecodedFrames.resize(static_cast<size_t>(sampleCount));

			ma_uint64 totalFramesRead = 0;
			while (totalFramesRead < frameCount) {
				ma_uint64 framesRead = 0;
				result = ma_decoder_read_pcm_frames(
					&decoder,
					m_DecodedFrames.data() + static_cast<size_t>(totalFramesRead * channelCount),
					frameCount - totalFramesRead,
					&framesRead);
				if (result != MA_SUCCESS || framesRead == 0) {
					break;
				}

				totalFramesRead += framesRead;
			}

			if (totalFramesRead == 0) {
				AIM_CORE_ERROR_TAG("Audio", "Failed to decode audio frames: {}", filepath);
				ma_decoder_uninit(&decoder);
				Cleanup();
				return false;
			}

			if (totalFramesRead < frameCount) {
				m_DecodedFrames.resize(static_cast<size_t>(totalFramesRead * channelCount));
				frameCount = totalFramesRead;
			}

			m_FrameCount = frameCount;
		}
		else {
			constexpr ma_uint64 chunkFrames = 4096;
			std::vector<float> decodedFrames;
			decodedFrames.reserve(static_cast<size_t>(chunkFrames * (m_Channels == 0 ? 1u : m_Channels)));

			while (true) {
				const size_t chunkSampleCount = static_cast<size_t>(chunkFrames * static_cast<ma_uint64>(m_Channels));
				const size_t oldSize = decodedFrames.size();
				decodedFrames.resize(oldSize + chunkSampleCount);

				ma_uint64 framesRead = 0;
				result = ma_decoder_read_pcm_frames(&decoder, decodedFrames.data() + oldSize, chunkFrames, &framesRead);
				if (result != MA_SUCCESS) {
					decodedFrames.clear();
					break;
				}

				decodedFrames.resize(oldSize + static_cast<size_t>(framesRead * m_Channels));
				if (framesRead == 0) {
					break;
				}
			}

			if (decodedFrames.empty()) {
				AIM_CORE_ERROR_TAG("Audio", "Failed to decode audio frames: {}", filepath);
				ma_decoder_uninit(&decoder);
				Cleanup();
				return false;
			}

			m_FrameCount = static_cast<ma_uint64>(decodedFrames.size() / m_Channels);
			m_DecodedFrames = std::move(decodedFrames);
		}

		ma_decoder_uninit(&decoder);
		m_Filepath = filepath;
		m_IsLoaded = true;

		return true;
	}

	uint32_t Audio::GetSampleRate() const {
		return m_IsLoaded ? m_SampleRate : 0;
	}

	uint32_t Audio::GetChannels() const {
		return m_IsLoaded ? m_Channels : 0;
	}

	uint64_t Audio::GetFrameCount() const {
		return m_IsLoaded ? m_FrameCount : 0;
	}

	float Audio::GetDurationSeconds() const {
		if (!m_IsLoaded) {
			return 0.0f;
		}

		uint64_t frameCount = GetFrameCount();
		uint32_t sampleRate = GetSampleRate();

		if (sampleRate == 0) {
			return 0.0f;
		}

		return static_cast<float>(frameCount) / static_cast<float>(sampleRate);
	}

	void Audio::Cleanup() {
		m_DecodedFrames.clear();
		m_DecodedFrames.shrink_to_fit();
		m_Format = ma_format_unknown;
		m_Channels = 0;
		m_SampleRate = 0;
		m_FrameCount = 0;
		m_IsLoaded = false;
		m_Filepath.clear();
	}
}

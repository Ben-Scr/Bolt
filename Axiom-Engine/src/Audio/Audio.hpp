#pragma once
#include <miniaudio.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Axiom {

    class Audio {
    public:
        Audio() = default;
        ~Audio();

        Audio(const Audio&) = delete;
        Audio& operator=(const Audio&) = delete;
        Audio(Audio&& other) noexcept;
        Audio& operator=(Audio&& other) noexcept;

        bool LoadFromFile(const std::string& filepath);

        const void* GetData() const { return m_DecodedFrames.empty() ? nullptr : m_DecodedFrames.data(); }
        ma_format GetFormat() const { return m_Format; }
        bool IsLoaded() const { return m_IsLoaded; }
        const std::string& GetFilepath() const { return m_Filepath; }


        uint32_t GetSampleRate() const;
        uint32_t GetChannels() const;
        uint64_t GetFrameCount() const;
        float GetDurationSeconds() const;

    private:
        std::vector<float> m_DecodedFrames;
        ma_format m_Format = ma_format_unknown;
        ma_uint32 m_Channels = 0;
        ma_uint32 m_SampleRate = 0;
        ma_uint64 m_FrameCount = 0;
        bool m_IsLoaded = false;
        std::string m_Filepath;

        void Cleanup();
    };
}

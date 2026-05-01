#pragma once
 
namespace Axiom {

    class AudioHandle {
    public:
        using HandleType = uint32_t;
        static constexpr HandleType Invalid = 0;

        AudioHandle() : m_handle(Invalid) {}
        explicit AudioHandle(HandleType handle) : m_handle(handle) {}

        bool IsValid() const { return m_handle != Invalid; }
        HandleType GetHandle() const { return m_handle; }

        bool operator==(const AudioHandle& other) const { return m_handle == other.m_handle; }
        bool operator!=(const AudioHandle& other) const { return m_handle != other.m_handle; }

    private:
        HandleType m_handle;
    };
}
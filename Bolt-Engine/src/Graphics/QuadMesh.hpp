#pragma once
#include "Graphics/Instance44.hpp"

#include <cstddef>
#include <span>

namespace Bolt {
    class QuadMesh {
    public:
        void Initialize();
        void Bind() const;
        void Unbind() const;
        void Draw() const;
        void DrawInstanced(std::size_t instanceCount) const;
        void UploadInstances(std::span<const Instance44> instances);
        void Shutdown();

    private:
        unsigned m_VAO{ 0 };
        unsigned m_VBO{ 0 };
        unsigned m_EBO{ 0 };
        unsigned m_InstanceVBO{ 0 };
        std::size_t m_InstanceCapacity = 0;
    };
}

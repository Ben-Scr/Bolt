#include "pch.hpp"
#include "QuadMesh.hpp"
#include <glad/glad.h>

namespace Bolt {
    namespace {
        struct V2UV {
            float x, y;
            float u, v;
        };

        constexpr V2UV QUAD_VERTICES[4] = {
            {-0.5f, -0.5f, 0.0f, 0.0f},
            { 0.5f, -0.5f, 1.0f, 0.0f},
            { 0.5f,  0.5f, 1.0f, 1.0f},
            {-0.5f,  0.5f, 0.0f, 1.0f},
        };

        constexpr unsigned short QUAD_INDICES[6] = { 0, 1, 2, 0, 2, 3 };

        std::size_t NextInstanceCapacity(std::size_t requiredCapacity) {
            std::size_t capacity = 256;
            while (capacity < requiredCapacity) {
                capacity *= 2;
            }
            return capacity;
        }
    }

    void QuadMesh::Initialize() {
        if (m_VAO != 0) {
            return;
        }

        glGenVertexArrays(1, &m_VAO);
        glGenBuffers(1, &m_VBO);
        glGenBuffers(1, &m_EBO);
        glGenBuffers(1, &m_InstanceVBO);

        glBindVertexArray(m_VAO);

        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTICES), QUAD_VERTICES, GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(QUAD_INDICES), QUAD_INDICES, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(V2UV), reinterpret_cast<void*>(offsetof(V2UV, x)));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V2UV), reinterpret_cast<void*>(offsetof(V2UV, u)));

        glBindBuffer(GL_ARRAY_BUFFER, m_InstanceVBO);
        m_InstanceCapacity = 256;
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(Instance44) * m_InstanceCapacity), nullptr, GL_DYNAMIC_DRAW);

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Instance44), reinterpret_cast<void*>(offsetof(Instance44, Color)));
        glVertexAttribDivisor(2, 1);

        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Instance44), reinterpret_cast<void*>(offsetof(Instance44, Position)));
        glVertexAttribDivisor(3, 1);

        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(Instance44), reinterpret_cast<void*>(offsetof(Instance44, Scale)));
        glVertexAttribDivisor(4, 1);

        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(Instance44), reinterpret_cast<void*>(offsetof(Instance44, Rotation)));
        glVertexAttribDivisor(5, 1);

        glBindVertexArray(0);
    }

    void QuadMesh::Bind() const {
        glBindVertexArray(m_VAO);
    }

    void QuadMesh::Unbind() const {
        glBindVertexArray(0);
    }

    void QuadMesh::Draw() const {
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
    }

    void QuadMesh::DrawInstanced(std::size_t instanceCount) const {
        if (instanceCount == 0) {
            return;
        }

        glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr, static_cast<GLsizei>(instanceCount));
    }

    void QuadMesh::UploadInstances(std::span<const Instance44> instances) {
        if (instances.empty()) {
            return;
        }

        glBindBuffer(GL_ARRAY_BUFFER, m_InstanceVBO);

        if (instances.size() > m_InstanceCapacity) {
            m_InstanceCapacity = NextInstanceCapacity(instances.size());
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(Instance44) * m_InstanceCapacity), nullptr, GL_DYNAMIC_DRAW);
        }

        glBufferSubData(
            GL_ARRAY_BUFFER,
            0,
            static_cast<GLsizeiptr>(instances.size_bytes()),
            instances.data());
    }

    void QuadMesh::Shutdown() {
        if (m_InstanceVBO) {
            glDeleteBuffers(1, &m_InstanceVBO);
            m_InstanceVBO = 0;
            m_InstanceCapacity = 0;
        }
        if (m_EBO) {
            glDeleteBuffers(1, &m_EBO);
            m_EBO = 0;
        }
        if (m_VBO) {
            glDeleteBuffers(1, &m_VBO);
            m_VBO = 0;
        }
        if (m_VAO) {
            glDeleteVertexArrays(1, &m_VAO);
            m_VAO = 0;
        }
    }
}

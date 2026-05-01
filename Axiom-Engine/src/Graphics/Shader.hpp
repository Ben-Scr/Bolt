#pragma once
#include <string>
#include <cstdint>
#include <glad/glad.h>

namespace Axiom {
    class Shader {
    public:
        Shader(const std::string& vsPath, const std::string& fsPath);
        ~Shader();

        static Shader FromBinary(const std::string& binaryPath);
        bool ExportBinary(const std::string& outputPath) const;
        static Shader LoadWithBinaryCache(
            const std::string& binaryPath,
            const std::string& vsPath,
            const std::string& fsPath);

        void Submit() const;

        GLuint GetHandle() const { return m_Program; }
        bool IsValid() const { return m_IsValid && m_Program != 0; }

        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;
        Shader(Shader&&) noexcept;
        Shader& operator=(Shader&&) noexcept;

    private:
        explicit Shader(GLuint program);
        static GLuint LoadAndCompile(GLenum type, const std::string& path);
        GLuint m_Program{ 0 };
        bool m_IsValid{ false };
    };
}
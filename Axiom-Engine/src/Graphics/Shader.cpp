#include "pch.hpp"
#include "Shader.hpp"

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace Axiom {
    namespace {
        constexpr uint32_t kAxiomShaderMagic   = 0x42534842; // "BSHB"
        constexpr uint32_t kAxiomShaderVersion = 1;

        struct BinaryHeader {
            uint32_t magic;
            uint32_t version;
            GLenum   binaryFormat;
            GLsizei  dataLength;
        };
        static_assert(sizeof(BinaryHeader) == 16);
    }

    static bool ReadFileToString(const std::string& path, std::string& out) {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            return false;
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        out = buffer.str();
        return !out.empty();
    }

    GLuint Shader::LoadAndCompile(GLenum type, const std::string& path) {
        std::string src;

        if (!ReadFileToString(path, src)) {
            AIM_ERROR_TAG("Shader", "Failed to read shader file: " + path);
            return 0;
        }

        GLuint shader = glCreateShader(type);
        if (shader == 0) {
           AIM_ERROR_TAG("Shader", "Failed to create shader object for file: " + path);
            return 0;
        }
        const char* csrc = src.c_str();
        glShaderSource(shader, 1, &csrc, nullptr);
        glCompileShader(shader);

        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

        if (ok != GL_TRUE) {
            GLint logLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<GLchar> log(std::max(1, logLen));
            glGetShaderInfoLog(shader, logLen, nullptr, log.data());

            const char* typeStr = (type == GL_VERTEX_SHADER) ? "vertex" :
                (type == GL_FRAGMENT_SHADER) ? "fragment" : "unknown";
            AIM_ERROR_TAG("Shader", std::string("Compile failed (") + typeStr + "): " + path + "\n" + log.data());

            glDeleteShader(shader);
            return 0;
        }

        return shader;
    }

    Shader::Shader(const std::string& vsPath, const std::string& fsPath) {
        GLuint vs = LoadAndCompile(GL_VERTEX_SHADER, vsPath);
        if (vs == 0) return;

        GLuint fs = LoadAndCompile(GL_FRAGMENT_SHADER, fsPath);
        if (fs == 0) { glDeleteShader(vs); return; }

        m_Program = glCreateProgram();
        if (m_Program == 0) {
            AIM_ERROR_TAG("Shader", "Failed to create shader program for files: " + vsPath + " + " + fsPath);
            glDeleteShader(vs);
            glDeleteShader(fs);
            return;
        }
        glAttachShader(m_Program, vs);
        glAttachShader(m_Program, fs);
        glLinkProgram(m_Program);

        glDeleteShader(vs);
        glDeleteShader(fs);

        GLint linked = GL_FALSE;
        glGetProgramiv(m_Program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            GLint logLen = 0;
            glGetProgramiv(m_Program, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<GLchar> log(std::max(1, logLen));
            glGetProgramInfoLog(m_Program, logLen, nullptr, log.data());

            AIM_ERROR_TAG("Shader", std::string("Program link failed : ") + vsPath + " + " + fsPath + "\n" + log.data());

            glDeleteProgram(m_Program);
            m_Program = 0;
            return;
        }

        m_IsValid = true;
    }

    Shader::~Shader() {
        if (m_Program != 0) {
            glDeleteProgram(m_Program);
            m_Program = 0;
        }
    }

    Shader::Shader(Shader&& o) noexcept {
        m_Program = o.m_Program;
        m_IsValid = o.m_IsValid;
        o.m_Program = 0;
        o.m_IsValid = false;
    }

    Shader& Shader::operator=(Shader&& o) noexcept {
        if (this != &o) {
            if (m_Program != 0) {
                glDeleteProgram(m_Program);
            }
            m_Program = o.m_Program;
            m_IsValid = o.m_IsValid;
            o.m_Program = 0;
            o.m_IsValid = false;
        }
        return *this;
    }

    void Shader::Submit() const {
        if (IsValid()) {
            glUseProgram(m_Program);
        }
        else {
            glUseProgram(0);
        }
    }

    Shader::Shader(GLuint program)
        : m_Program(program)
        , m_IsValid(program != 0)
    {
    }

    Shader Shader::FromBinary(const std::string& binaryPath) {
        std::ifstream file(binaryPath, std::ios::binary);
        if (!file.is_open()) {
            return Shader(static_cast<GLuint>(0));
        }

        BinaryHeader header{};
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!file || header.magic != kAxiomShaderMagic || header.version != kAxiomShaderVersion || header.dataLength <= 0) {
            AIM_CORE_WARN_TAG("Shader", "Invalid binary shader header: " + binaryPath);
            return Shader(static_cast<GLuint>(0));
        }

        std::vector<uint8_t> data(header.dataLength);
        file.read(reinterpret_cast<char*>(data.data()), header.dataLength);
        if (!file) {
            AIM_CORE_WARN_TAG("Shader", "Truncated binary shader file: " + binaryPath);
            return Shader(static_cast<GLuint>(0));
        }

        GLuint program = glCreateProgram();
        if (program == 0) {
            AIM_ERROR_TAG("Shader", "Failed to create program for binary shader: " + binaryPath);
            return Shader(static_cast<GLuint>(0));
        }

        glProgramBinary(program, header.binaryFormat, data.data(), header.dataLength);

        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            AIM_CORE_WARN_TAG("Shader", "Binary shader validation failed (driver/GPU change?): " + binaryPath);
            glDeleteProgram(program);
            return Shader(static_cast<GLuint>(0));
        }

        return Shader(program);
    }

    bool Shader::ExportBinary(const std::string& outputPath) const {
        if (!IsValid()) {
            AIM_ERROR_TAG("Shader", "Cannot export invalid shader program");
            return false;
        }

        GLint binaryLength = 0;
        glGetProgramiv(m_Program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
        if (binaryLength <= 0) {
            AIM_CORE_WARN_TAG("Shader", "Driver does not support program binary retrieval");
            return false;
        }

        std::vector<uint8_t> data(binaryLength);
        GLenum binaryFormat = 0;
        GLsizei actualLength = 0;
        glGetProgramBinary(m_Program, binaryLength, &actualLength, &binaryFormat, data.data());

        if (actualLength <= 0) {
            AIM_CORE_WARN_TAG("Shader", "glGetProgramBinary returned no data");
            return false;
        }

        BinaryHeader header{};
        header.magic = kAxiomShaderMagic;
        header.version = kAxiomShaderVersion;
        header.binaryFormat = binaryFormat;
        header.dataLength = actualLength;

        std::ofstream file(outputPath, std::ios::binary);
        if (!file.is_open()) {
            AIM_CORE_WARN_TAG("Shader", "Cannot write binary shader to: " + outputPath);
            return false;
        }

        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(data.data()), actualLength);
        return file.good();
    }

    Shader Shader::LoadWithBinaryCache(
        const std::string& binaryPath,
        const std::string& vsPath,
        const std::string& fsPath)
    {
        if (std::filesystem::exists(binaryPath)) {
            Shader cached = FromBinary(binaryPath);
            if (cached.IsValid()) {
                AIM_INFO_TAG("Shader", "Loaded precompiled shader: " + binaryPath);
                return cached;
            }
            AIM_CORE_WARN_TAG("Shader", "Stale binary shader, recompiling from source");
        }

        Shader shader(vsPath, fsPath);
        if (shader.IsValid()) {
            if (shader.ExportBinary(binaryPath)) {
                AIM_INFO_TAG("Shader", "Cached compiled shader: " + binaryPath);
            }
        }
        return shader;
    }

}
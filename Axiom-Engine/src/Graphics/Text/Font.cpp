#include "pch.hpp"
#include "Graphics/Text/Font.hpp"
#include "Serialization/File.hpp"

#include <glad/glad.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cstring>

namespace Axiom {
    namespace {
        // Codepoint range baked into the atlas. ASCII printable only for
        // the MVP; once SDF/per-codepoint rasterization lands, replace
        // this with on-demand glyph emission.
        constexpr uint32_t k_FirstCodepoint = 32;   // space
        constexpr uint32_t k_LastCodepoint = 126;   // tilde
        constexpr int k_CodepointCount = static_cast<int>(k_LastCodepoint - k_FirstCodepoint + 1);

        // Atlas size: square power-of-two large enough for ASCII at
        // typical UI sizes (32-64 px). Larger fonts get a bigger atlas
        // automatically; 1024² is the cap before we reject as "use a
        // smaller pixel size or wait for SDF support".
        constexpr int k_AtlasInitialSide = 256;
        constexpr int k_AtlasMaxSide = 1024;
    }

    Font::~Font() {
        Cleanup();
    }

    Font::Font(Font&& other) noexcept
        : m_TtfBuffer(std::move(other.m_TtfBuffer))
        , m_Glyphs(std::move(other.m_Glyphs))
        , m_AtlasTexture(other.m_AtlasTexture)
        , m_AtlasWidth(other.m_AtlasWidth)
        , m_AtlasHeight(other.m_AtlasHeight)
        , m_PixelSize(other.m_PixelSize)
        , m_Ascent(other.m_Ascent)
        , m_Descent(other.m_Descent)
        , m_LineHeight(other.m_LineHeight)
        , m_StbScale(other.m_StbScale)
        , m_StbFontInfoStorage(std::move(other.m_StbFontInfoStorage))
        , m_Filepath(std::move(other.m_Filepath))
    {
        other.m_AtlasTexture = 0;
        other.m_AtlasWidth = 0;
        other.m_AtlasHeight = 0;
        other.m_PixelSize = 0.0f;
        other.m_Ascent = 0.0f;
        other.m_Descent = 0.0f;
        other.m_LineHeight = 0.0f;
        other.m_StbScale = 0.0f;
    }

    Font& Font::operator=(Font&& other) noexcept {
        if (this != &other) {
            Cleanup();

            m_TtfBuffer = std::move(other.m_TtfBuffer);
            m_Glyphs = std::move(other.m_Glyphs);
            m_AtlasTexture = other.m_AtlasTexture;
            m_AtlasWidth = other.m_AtlasWidth;
            m_AtlasHeight = other.m_AtlasHeight;
            m_PixelSize = other.m_PixelSize;
            m_Ascent = other.m_Ascent;
            m_Descent = other.m_Descent;
            m_LineHeight = other.m_LineHeight;
            m_StbScale = other.m_StbScale;
            m_StbFontInfoStorage = std::move(other.m_StbFontInfoStorage);
            m_Filepath = std::move(other.m_Filepath);

            other.m_AtlasTexture = 0;
            other.m_AtlasWidth = 0;
            other.m_AtlasHeight = 0;
            other.m_PixelSize = 0.0f;
            other.m_Ascent = 0.0f;
            other.m_Descent = 0.0f;
            other.m_LineHeight = 0.0f;
            other.m_StbScale = 0.0f;
        }
        return *this;
    }

    bool Font::LoadFromFile(const std::string& path, float pixelSize) {
        if (path.empty() || pixelSize <= 0.0f) {
            return false;
        }

        Cleanup();

        if (!File::Exists(path)) {
            AIM_CORE_ERROR_TAG("Font", "TTF not found: {}", path);
            return false;
        }

        // Read the whole .ttf into memory — stb_truetype keeps a pointer
        // into this buffer for the lifetime of the fontinfo, so we keep
        // it alive as a Font member.
        m_TtfBuffer = File::ReadAllBytes(path);
        if (m_TtfBuffer.empty()) {
            AIM_CORE_ERROR_TAG("Font", "TTF empty / unreadable: {}", path);
            return false;
        }

        // Reserve fontinfo storage. stbtt_fontinfo size is fixed at compile
        // time; reserve the storage as raw bytes to avoid pulling stb's
        // header into Font.hpp.
        m_StbFontInfoStorage.assign(sizeof(stbtt_fontinfo), 0);
        stbtt_fontinfo* font = reinterpret_cast<stbtt_fontinfo*>(m_StbFontInfoStorage.data());

        if (!stbtt_InitFont(font, m_TtfBuffer.data(), stbtt_GetFontOffsetForIndex(m_TtfBuffer.data(), 0))) {
            AIM_CORE_ERROR_TAG("Font", "stbtt_InitFont failed for: {}", path);
            m_TtfBuffer.clear();
            m_StbFontInfoStorage.clear();
            return false;
        }

        const float scale = stbtt_ScaleForPixelHeight(font, pixelSize);
        m_StbScale = scale;
        m_PixelSize = pixelSize;

        int ascent = 0;
        int descent = 0;
        int lineGap = 0;
        stbtt_GetFontVMetrics(font, &ascent, &descent, &lineGap);
        m_Ascent = ascent * scale;
        m_Descent = descent * scale;
        m_LineHeight = (ascent - descent + lineGap) * scale;

        // Bake atlas. Try increasing sizes until pack succeeds.
        std::vector<stbtt_packedchar> packed(k_CodepointCount);
        std::vector<uint8_t> bitmap;
        int side = k_AtlasInitialSide;
        bool ok = false;
        while (side <= k_AtlasMaxSide) {
            bitmap.assign(static_cast<size_t>(side) * static_cast<size_t>(side), 0);

            stbtt_pack_context pctx{};
            if (!stbtt_PackBegin(&pctx, bitmap.data(), side, side, 0, 1, nullptr)) {
                AIM_CORE_ERROR_TAG("Font", "stbtt_PackBegin failed at side={}", side);
                break;
            }
            stbtt_PackSetOversampling(&pctx, 2, 2);

            const int packResult = stbtt_PackFontRange(
                &pctx,
                m_TtfBuffer.data(),
                0,
                pixelSize,
                static_cast<int>(k_FirstCodepoint),
                k_CodepointCount,
                packed.data());
            stbtt_PackEnd(&pctx);

            if (packResult) {
                m_AtlasWidth = side;
                m_AtlasHeight = side;
                ok = true;
                break;
            }
            side *= 2;
        }

        if (!ok) {
            AIM_CORE_ERROR_TAG("Font", "Atlas packing failed (font too large for max atlas {}px): {}", k_AtlasMaxSide, path);
            m_TtfBuffer.clear();
            m_StbFontInfoStorage.clear();
            return false;
        }

        // Upload as GL_RED (single-channel). Linear filter keeps glyphs
        // smooth at non-baked sizes; clamp-to-edge avoids bleeding when
        // a glyph sits at the atlas border.
        GLint savedUnpackAlign = 4;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &savedUnpackAlign);
        glGenTextures(1, &m_AtlasTexture);
        glBindTexture(GL_TEXTURE_2D, m_AtlasTexture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
            m_AtlasWidth, m_AtlasHeight, 0,
            GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, savedUnpackAlign);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Translate stb's packed table into our codepoint→GlyphMetrics map.
        // Note on the oversampling-vs-domain mismatch:
        //   pc.x0/x1/y0/y1 are *atlas-pixel* coordinates, which include the
        //     2× oversample factor applied above (so a 32-px glyph occupies
        //     ~64 atlas pixels of width).
        //   pc.xoff/xoff2/yoff/yoff2 and pc.xadvance are in *screen-pixel*
        //     domain (oversample already divided out by stb).
        // We must use xoff2-xoff / yoff2-yoff for the rendered quad width
        // and height — using x1-x0 directly would render glyphs at 2× the
        // intended size and they'd overlap horizontally.
        for (int i = 0; i < k_CodepointCount; ++i) {
            const stbtt_packedchar& pc = packed[i];
            GlyphMetrics m;
            m.U0 = static_cast<float>(pc.x0) / static_cast<float>(m_AtlasWidth);
            m.V0 = static_cast<float>(pc.y0) / static_cast<float>(m_AtlasHeight);
            m.U1 = static_cast<float>(pc.x1) / static_cast<float>(m_AtlasWidth);
            m.V1 = static_cast<float>(pc.y1) / static_cast<float>(m_AtlasHeight);
            m.Width = pc.xoff2 - pc.xoff;
            m.Height = pc.yoff2 - pc.yoff;
            m.XOffset = pc.xoff;
            m.YOffset = pc.yoff;
            m.XAdvance = pc.xadvance;
            m_Glyphs[k_FirstCodepoint + static_cast<uint32_t>(i)] = m;
        }

        m_Filepath = path;
        return true;
    }

    const GlyphMetrics* Font::GetGlyph(uint32_t codepoint) const {
        const auto it = m_Glyphs.find(codepoint);
        return it != m_Glyphs.end() ? &it->second : nullptr;
    }

    float Font::GetKerning(uint32_t a, uint32_t b) const {
        if (m_StbFontInfoStorage.empty()) {
            return 0.0f;
        }
        const stbtt_fontinfo* font = reinterpret_cast<const stbtt_fontinfo*>(m_StbFontInfoStorage.data());
        const int kern = stbtt_GetCodepointKernAdvance(font, static_cast<int>(a), static_cast<int>(b));
        return kern * m_StbScale;
    }

    void Font::Cleanup() {
        if (m_AtlasTexture) {
            glDeleteTextures(1, &m_AtlasTexture);
            m_AtlasTexture = 0;
        }
        m_AtlasWidth = 0;
        m_AtlasHeight = 0;
        m_Glyphs.clear();
        m_TtfBuffer.clear();
        m_StbFontInfoStorage.clear();
        m_PixelSize = 0.0f;
        m_Ascent = 0.0f;
        m_Descent = 0.0f;
        m_LineHeight = 0.0f;
        m_StbScale = 0.0f;
        m_Filepath.clear();
    }

}

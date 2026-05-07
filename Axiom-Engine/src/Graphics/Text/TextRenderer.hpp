#pragma once
#include "Collections/AABB.hpp"
#include "Collections/Color.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Core/Export.hpp"
#include "Graphics/Text/FontHandle.hpp"

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Axiom {

    class Scene;
    class Shader;
    class Font;

    // Per-vertex layout pushed to the GPU. Six of these per visible glyph,
    // built fresh every frame — keeps the implementation simple at a
    // small memory cost (~32 bytes × 6 verts × visible glyphs). When that
    // becomes a hot-path concern we'll move to instanced quads with a
    // per-instance UV-rect attribute.
    struct TextVertex {
        float X = 0.0f;
        float Y = 0.0f;
        float U = 0.0f;
        float V = 0.0f;
        float R = 1.0f;
        float G = 1.0f;
        float B = 1.0f;
        float A = 1.0f;
    };

    class AXIOM_API TextRenderer {
    public:
        TextRenderer();
        ~TextRenderer();

        TextRenderer(const TextRenderer&) = delete;
        TextRenderer& operator=(const TextRenderer&) = delete;

        // GL setup. Safe to call once a GL context is alive.
        void Initialize();

        // Render every TextRendererComponent in the scene whose AABB intersects
        // the supplied view frustum, transformed by `vp`. Called by
        // Renderer2D as part of its scene render pass — sits *after*
        // sprite submission so text always layers on top of sprites by
        // default. Within text, sorting follows (SortingLayer,
        // SortingOrder, font atlas) for batching efficiency.
        void RenderScene(Scene& scene, const glm::mat4& vp, const AABB& viewportAABB);

        // Drop GL state. Must run while the GL context is still alive.
        void Shutdown();

        bool IsInitialized() const { return m_IsInitialized; }
        size_t GetGlyphsRenderedLastFrame() const { return m_LastFrameGlyphCount; }
        size_t GetDrawCallsLastFrame() const { return m_LastFrameDrawCalls; }

    private:
        struct GlyphBatchKey {
            unsigned AtlasTexture = 0;
            int16_t SortingOrder = 0;
            uint8_t SortingLayer = 0;
        };

        struct GlyphRun {
            GlyphBatchKey Key{};
            size_t VertexStart = 0;
            size_t VertexCount = 0;
        };

        // Per-text scratch entry. Kept as a member type so the working buffer
        // can be reused across frames (see m_Pending) instead of reallocating
        // every RenderScene call.
        struct PendingText {
            Font* FontPtr = nullptr;
            const std::string* Text = nullptr;
            float WorldX = 0.0f;
            float WorldY = 0.0f;
            float Scale = 1.0f;
            float LetterSpacing = 0.0f;
            Color Tint{};
            TextAlignment Align = TextAlignment::Left;
            int16_t SortingOrder = 0;
            uint8_t SortingLayer = 0;
        };

        void EnsureGpuCapacity(size_t requiredBytes);
        void EmitText(Font& font, const std::string& text,
            float worldX, float worldY,
            float scale, const Color& color,
            TextAlignment alignment, float letterSpacing);

        bool m_IsInitialized = false;

        unsigned m_VAO = 0;
        unsigned m_VBO = 0;
        size_t m_VBOCapacity = 0;

        // Reused across frames so we don't allocate in the hot path.
        std::vector<TextVertex> m_Vertices;
        std::vector<GlyphRun> m_Runs;
        std::vector<PendingText> m_Pending;

        std::unique_ptr<Shader> m_Shader;
        int m_uMVP = -1;
        int m_uAtlas = -1;

        size_t m_LastFrameGlyphCount = 0;
        size_t m_LastFrameDrawCalls = 0;
    };

}

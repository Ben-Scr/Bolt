#pragma once
#include "Graphics/PosColorVertex.hpp"
#include "Graphics/Gizmo.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>
#include <cstdint>

namespace Axiom {
    class Camera2DComponent;
    class Shader;

    class GizmoRenderer2D {
    public:
        static bool Initialize();
        static void Shutdown();
        static void OnResize(int w, int h);
        static void BeginFrame(uint16_t viewId = 1);
        static void Render(GizmoLayerMask layerMask = GizmoLayerMask::Shared);
        static void EndFrame();

        static void RenderWithVP(const glm::mat4& vp, GizmoLayerMask layerMask = GizmoLayerMask::All);

    private:
        static void BuildGeometry(GizmoLayerMask layerMask);
        static void FlushGizmos();
        static void FlushGizmosWithVP(const glm::mat4& vp);

        static bool m_IsInitialized;
        static std::unique_ptr<Shader> m_GizmoShader;
        static std::vector<PosColorVertex> m_GizmoVertices;
        static std::vector<uint16_t> m_GizmoIndices;

        static uint16_t m_GizmoViewId;

        static unsigned int m_VAO;
        static unsigned int m_VBO;
        static unsigned int m_EBO;
        static int m_uMVP;
    };
}

using Axiom.Interop;

namespace Axiom.Graphics;

public static class Gizmo
{
    public static Color Color
    {
        get => GetColor();
        set => InternalCalls.Gizmo_SetColor(value.R, value.G, value.B, value.A);
    }

    internal static Color GetColor()
    {
        InternalCalls.Gizmo_GetColor(out float r, out float g, out float b, out float a);
        return new Color(r, g, b, a);
    }

    public static float LineWidth
    {
        get => InternalCalls.Gizmo_GetLineWidth();
        set => InternalCalls.Gizmo_SetLineWidth(value);
    }


    public static void DrawLine(Vector2 start, Vector2 end)
        => InternalCalls.Gizmo_DrawLine(start.X, start.Y, end.X, end.Y);

    public static void DrawSquare(Vector2 center, Vector2 size, float degrees = 0.0f)
        => InternalCalls.Gizmo_DrawSquare(center.X, center.Y, size.X, size.Y, degrees);

    public static void DrawCircle(Vector2 center, float radius, int segments = 32)
        => InternalCalls.Gizmo_DrawCircle(center.X, center.Y, radius, segments);
}

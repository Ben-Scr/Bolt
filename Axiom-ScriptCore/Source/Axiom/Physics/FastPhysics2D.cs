using System;

namespace Axiom.Physics;

public static class FastPhysics2D
{
    public static int MaxPolygonVertices
    {
        get => Physics2D.MaxPolygonVertices;
        set => Physics2D.MaxPolygonVertices = value;
    }

    public static RaycastHit2D Raycast(Vector2 origin, Vector2 direction, float maxDistance = Mathf.Infinity)
        => Physics2D.Raycast(origin, direction, maxDistance);

    public static bool RaycastCheck(Vector2 origin, Vector2 direction, float maxDistance = Mathf.Infinity)
        => Physics2D.RaycastCheck(origin, direction, maxDistance);

    public static Entity? OverlapCircle(Vector2 origin, float radius)
        => Physics2D.OverlapCircle(origin, radius);

    public static bool OverlapCircleCheck(Vector2 origin, float radius)
        => Physics2D.OverlapCircleCheck(origin, radius);

    public static Entity? OverlapBox(Vector2 origin, Vector2 size, float rotation = 0)
        => Physics2D.OverlapBox(origin, size, rotation);

    public static bool OverlapBoxCheck(Vector2 origin, Vector2 size, float rotation = 0)
        => Physics2D.OverlapBoxCheck(origin, size, rotation);

    public static Entity[] OverlapCircleAll(Vector2 origin, float radius)
        => Physics2D.OverlapCircleAll(origin, radius);

    public static Entity[] OverlapBoxAll(Vector2 origin, Vector2 size, float rotation = 0)
        => Physics2D.OverlapBoxAll(origin, size, rotation);

    public static Entity? OverlapPolygon(Vector2 origin, params float[] points)
        => Physics2D.OverlapPolygon(origin, points);

    public static bool OverlapPolygonCheck(Vector2 origin, params float[] points)
        => Physics2D.OverlapPolygonCheck(origin, points);

    public static Entity[] OverlapPolygonAll(Vector2 origin, params float[] points)
        => Physics2D.OverlapPolygonAll(origin, points);

    public static Entity? ContainsPoint(Vector2 origin)
        => Physics2D.ContainsPoint(origin);

    public static Entity[] ContainsPointAll(Vector2 origin)
        => Physics2D.ContainsPointAll(origin);

    public static bool ContainsPointCheck(Vector2 origin)
        => Physics2D.ContainsPointCheck(origin);

    [Obsolete("Use ContainsPoint.")]
    public static Entity? OverlapPoint(Vector2 origin) => ContainsPoint(origin);

    [Obsolete("Use ContainsPointAll.")]
    public static Entity[] OverlapPointAll(Vector2 origin) => ContainsPointAll(origin);

    [Obsolete("Use ContainsPointCheck.")]
    public static bool OverlapPointCheck(Vector2 origin) => ContainsPointCheck(origin);
}

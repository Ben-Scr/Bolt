using System;
using static Bolt.Physics.Physics2DShared;

namespace Bolt.Physics;

public struct RaycastHit2D
{
    public Entity? Entity;
    public Vector2 Point;
    public Vector2 Normal;
    public float Distance;
    public bool Hit;
}

// Box2D Physics Checks
public static class Physics2D
{
    public static int MaxPolygonVertices = 8;

    public static RaycastHit2D Raycast(Vector2 origin, Vector2 direction, float maxDistance = Mathf.Infinity)
    {
        RaycastHit2D result = new();

        if (!IsValidDirection(direction) || !IsValidDistance(maxDistance))
            return result;

        bool hit = InternalCalls.Physics2D_Raycast(
            origin.X, origin.Y,
            direction.X, direction.Y,
            maxDistance,
            out ulong hitEntityID,
            out float hitX, out float hitY,
            out float hitNormalX, out float hitNormalY,
            out float distance
        );

        result.Hit = hit;
        if (hit)
        {
            result.Entity = ToEntity(hitEntityID);
            result.Point = new Vector2(hitX, hitY);
            result.Normal = new Vector2(hitNormalX, hitNormalY);
            result.Distance = distance;
        }

        return result;
    }

    public static bool RaycastCheck(Vector2 origin, Vector2 direction, float maxDistance = Mathf.Infinity)
    {
        return Raycast(origin, direction, maxDistance).Hit;
    }

    public static Entity? OverlapCircle(Vector2 origin, float radius)
    {
        if (radius <= 0.0f || float.IsNaN(radius))
            return null;

        return InternalCalls.Physics2D_OverlapCircle(origin.X, origin.Y, radius, 0, out ulong entityID)
            ? ToEntity(entityID)
            : null;
    }

    public static bool OverlapCircleCheck(Vector2 origin, float radius)
    {
        return OverlapCircle(origin, radius) != null;
    }

    public static Entity? OverlapBox(Vector2 origin, Vector2 size, float rotation = 0)
    {
        if (size.X <= 0.0f || size.Y <= 0.0f || float.IsNaN(rotation))
            return null;

        Vector2 halfExtents = size * 0.5f;
        return InternalCalls.Physics2D_OverlapBox(origin.X, origin.Y, halfExtents.X, halfExtents.Y, rotation, 0, out ulong entityID)
            ? ToEntity(entityID)
            : null;
    }

    public static bool OverlapBoxCheck(Vector2 origin, Vector2 size, float rotation = 0)
    {
        return OverlapBox(origin, size, rotation) != null;
    }

    public static Entity[] OverlapCircleAll(Vector2 origin, float radius)
    {
        if (radius <= 0.0f || float.IsNaN(radius))
            return Array.Empty<Entity>();

        return ToEntities(buffer => InternalCalls.Physics2D_OverlapCircleAll(
            origin.X, origin.Y, radius, buffer));
    }

    public static Entity[] OverlapBoxAll(Vector2 origin, Vector2 size, float rotation = 0)
    {
        if (size.X <= 0.0f || size.Y <= 0.0f || float.IsNaN(rotation))
            return Array.Empty<Entity>();

        Vector2 halfExtents = size * 0.5f;
        return ToEntities(buffer => InternalCalls.Physics2D_OverlapBoxAll(
            origin.X, origin.Y, halfExtents.X, halfExtents.Y, rotation, buffer));
    }

    public static Entity? OverlapPolygon(Vector2 origin, params float[] points)
    {
        if (!IsValidPolygon(points, MaxPolygonVertices))
            return null;

        return InternalCalls.Physics2D_OverlapPolygon(origin.X, origin.Y, points, 0, out ulong entityID)
            ? ToEntity(entityID)
            : null;
    }

    public static bool OverlapPolygonCheck(Vector2 origin, params float[] points)
    {
        return OverlapPolygon(origin, points) != null;
    }

    public static Entity[] OverlapPolygonAll(Vector2 origin, params float[] points)
    {
        if (!IsValidPolygon(points, MaxPolygonVertices))
            return Array.Empty<Entity>();

        return ToEntities(buffer => InternalCalls.Physics2D_OverlapPolygonAll(origin.X, origin.Y, points, buffer));
    }

    public static Entity? ContainsPoint(Vector2 origin)
    {
        return InternalCalls.Physics2D_ContainsPoint(origin.X, origin.Y, 0, out ulong entityID)
            ? ToEntity(entityID)
            : null;
    }

    public static Entity[] ContainsPointAll(Vector2 origin)
    {
        return ToEntities(buffer => InternalCalls.Physics2D_ContainsPointAll(origin.X, origin.Y, buffer));
    }

    public static bool ContainsPointCheck(Vector2 origin)
    {
        return ContainsPoint(origin) != null;
    }

    [Obsolete("Use ContainsPoint.")]
    public static Entity? OverlapPoint(Vector2 origin) => ContainsPoint(origin);

    [Obsolete("Use ContainsPointAll.")]
    public static Entity[] OverlapPointAll(Vector2 origin) => ContainsPointAll(origin);

    [Obsolete("Use ContainsPointCheck.")]
    public static bool OverlapPointCheck(Vector2 origin) => ContainsPointCheck(origin);
}

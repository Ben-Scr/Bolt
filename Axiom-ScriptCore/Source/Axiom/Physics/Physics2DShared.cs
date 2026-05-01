using System;
using Axiom.Interop;

namespace Axiom.Physics;

internal static class Physics2DShared
{
    internal delegate int OverlapQuery(Span<ulong> buffer);

    internal static Entity? ToEntity(ulong entityID)
    {
        return entityID == 0 ? null : new Entity(entityID);
    }

    internal static bool IsValidDistance(float maxDistance)
    {
        return maxDistance > 0.0f && !float.IsNaN(maxDistance);
    }

    internal static bool IsValidDirection(Vector2 direction)
    {
        return direction.LengthSquared() > Mathf.Epsilon * Mathf.Epsilon;
    }

    internal static Entity[] ToEntities(OverlapQuery query)
    {
        ulong[] ids = ExecuteOverlapQuery(query);
        if (ids.Length == 0)
            return Array.Empty<Entity>();

        Entity[] entities = new Entity[ids.Length];
        for (int i = 0; i < ids.Length; i++)
            entities[i] = new Entity(ids[i]);
        return entities;
    }

    internal static bool IsValidPolygon(float[]? points, int maxVertices)
    {
        if (points == null || points.Length < 6 || points.Length % 2 != 0)
            return false;

        int vertexCount = points.Length / 2;
        if (vertexCount > maxVertices)
            return false;

        for (int i = 0; i < points.Length; i++)
        {
            if (float.IsNaN(points[i]) || float.IsInfinity(points[i]))
                return false;
        }

        return true;
    }

    private static ulong[] ExecuteOverlapQuery(OverlapQuery query)
    {
        int capacity = Math.Max(16, InternalCalls.Scene_GetEntityCount());
        ulong[] buffer = new ulong[capacity];

        while (true)
        {
            int count = query(buffer);
            if (count <= 0)
                return Array.Empty<ulong>();

            if (count <= buffer.Length)
            {
                if (count == buffer.Length)
                    return buffer;

                ulong[] result = new ulong[count];
                Array.Copy(buffer, result, count);
                return result;
            }

            buffer = new ulong[count];
        }
    }
}

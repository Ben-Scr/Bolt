

using System;

namespace Bolt.Physics
{
    public static class FastPhysics2D
    {
        public static int MaxPolygonVertices = 8;

        private static Entity? ToEntity(ulong entityID)
        {
            return entityID == 0 ? null : new Entity(entityID);
        }

        private static bool IsValidDistance(float maxDistance)
        {
            return maxDistance > 0.0f && !float.IsNaN(maxDistance);
        }

        private static bool IsValidDirection(Vector2 direction)
        {
            return direction.LengthSquared() > Mathf.Epsilon * Mathf.Epsilon;
        }

        public static RaycastHit2D Raycast(Vector2 origin, Vector2 direction, float maxDistance = Mathf.Infinity)
        {
            throw new System.NotImplementedException();
        }

        public static bool RaycastCheck(Vector2 origin, Vector2 direction, float maxDistance = Mathf.Infinity)
        {
            throw new System.NotImplementedException();
        }

        public static Entity? OverlapCircle(Vector2 origin, float radius)
        {

            throw new System.NotImplementedException();
        }

        public static bool OverlapCircleCheck(Vector2 origin, float radius)
        {
            throw new System.NotImplementedException();
        }

        public static Entity? OverlapBox(Vector2 origin, Vector2 size, float rotation = 0)
        {

            throw new System.NotImplementedException();
        }

        public static bool OverlapBoxCheck(Vector2 origin, Vector2 size, float rotation = 0)
        {

            throw new System.NotImplementedException();
        }

        public static Entity[] OverlapCircleAll(Vector2 origin, float radius)
        {

            throw new System.NotImplementedException();
        }

        public static Entity[] OverlapBoxAll(Vector2 origin, Vector2 size, float rotation = 0)
        {

            throw new System.NotImplementedException();
        }

        public static Entity? OverlapPolygon(Vector2 origin, params float[] points)
        {

            throw new System.NotImplementedException();
        }

        public static bool OverlapPolygonCheck(Vector2 origin, params float[] points)
        {
            throw new System.NotImplementedException();
        }

        public static Entity[] OverlapPolygonAll(Vector2 origin, params float[] points)
        {

            throw new System.NotImplementedException();
        }

        public static Entity? OverlapPoint(Vector2 origin)
        {
            throw new System.NotImplementedException();
        }
        public static Entity[] OverlapPointAll(Vector2 origin)
        {
            throw new System.NotImplementedException();
        }
        public static bool OverlapPointCheck(Vector2 origin)
        {
            throw new System.NotImplementedException();
        }


        private delegate int OverlapQuery(Span<ulong> buffer);

        private static Entity[] ToEntities(OverlapQuery query)
        {
            ulong[] ids = ExecuteOverlapQuery(query);
            if (ids.Length == 0)
                return Array.Empty<Entity>();

            Entity[] entities = new Entity[ids.Length];
            for (int i = 0; i < ids.Length; i++)
                entities[i] = new Entity(ids[i]);
            return entities;
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

        private static bool IsValidPolygon(float[] points)
        {
            if (points == null || points.Length < 6 || points.Length % 2 != 0)
                return false;

            int vertexCount = points.Length / 2;
            if (vertexCount > MaxPolygonVertices)
                return false;

            for (int i = 0; i < points.Length; i++)
            {
                if (float.IsNaN(points[i]) || float.IsInfinity(points[i]))
                    return false;
            }

            return true;
        }
    }
}

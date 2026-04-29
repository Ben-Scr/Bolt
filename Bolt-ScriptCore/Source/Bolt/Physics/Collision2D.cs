namespace Bolt;

public readonly struct Collision2D
{
    internal Collision2D(ulong selfEntityId, ulong otherEntityId, ulong entityAId, ulong entityBId, Vector2 contactPoint)
    {
        Self = new Entity(selfEntityId);
        Other = new Entity(otherEntityId);
        EntityA = new Entity(entityAId);
        EntityB = new Entity(entityBId);
        ContactPoint = contactPoint;
    }

    public Entity Self { get; }
    public Entity Other { get; }
    public Entity EntityA { get; }
    public Entity EntityB { get; }
    public Vector2 ContactPoint { get; }
}

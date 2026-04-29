namespace Bolt;

public readonly struct Collision2D
{
    internal Collision2D(ulong selfEntityId, ulong otherEntityId, ulong entityAId, ulong entityBId)
    {
        Self = new Entity(selfEntityId);
        Other = new Entity(otherEntityId);
        EntityA = new Entity(entityAId);
        EntityB = new Entity(entityBId);
    }

    public Entity Self { get; }
    public Entity Other { get; }
    public Entity EntityA { get; }
    public Entity EntityB { get; }
}

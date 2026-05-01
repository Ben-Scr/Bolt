using System;
using Axiom.Interop;

namespace Axiom;
public abstract class Component
{
    public Entity Entity { get; internal set; } = Entity.Invalid;

    protected ulong RequireComponent<TComponent>() where TComponent : Component, new()
    {
        if (Entity == null || Entity == Entity.Invalid || !InternalCalls.Entity_IsValid(Entity.ID))
            throw new InvalidOperationException($"{typeof(TComponent).Name} is no longer attached to a live entity.");

        if (!Entity.HasComponent<TComponent>())
            throw new InvalidOperationException($"{typeof(TComponent).Name} is no longer attached to entity {Entity.ID}.");

        return Entity.ID;
    }

    internal void Invalidate() => Entity = Entity.Invalid;
}

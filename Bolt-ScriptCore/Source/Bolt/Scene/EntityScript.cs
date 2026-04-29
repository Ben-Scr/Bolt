

namespace Bolt;

/// <summary>
/// Base class for user C# scripts. Subclass and implement Start()/Update()/OnDestroy().
/// </summary>
public abstract class EntityScript
{
    public Entity Entity { get; internal set; } = Entity.Invalid;
    public Transform2DComponent Transform { get; internal set; }

    internal void _SetEntityID(ulong id)
    {
        Entity = new Entity(id);
    }

    protected Entity? FindEntityByName(string name) => Entity.FindByName(name);

    protected T? GetComponent<T>() where T : Component, new() => Entity.GetComponent<T>();
    protected T? AddComponent<T>() where T : Component, new() => Entity.AddComponent<T>(); 

    protected Entity Create(string name = "") => Entity.Create(name);
    protected Entity Create(Entity source) => Entity.Create(source);

    public virtual void OnApplicationStart() { }
    public virtual void OnApplicationPaused() { }
    public virtual void OnFocusChanged(bool focused) { }

    public virtual void OnEnable()  { }
    public virtual void OnDisable() { }

    public virtual void OnCollisionEnter2D() { }
    public virtual void OnCollisionExit2D() { }
}

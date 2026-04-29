

namespace Bolt;

/// <summary>
/// Base class for user C# scripts. Subclass and override lifecycle callbacks.
/// </summary>
public abstract class EntityScript
{
    public Entity Entity { get; internal set; } = Entity.Invalid;
    public Transform2DComponent Transform => Entity.Transform;

    internal void _SetEntityID(ulong id)
    {
        Entity = new Entity(id);
    }

    protected Entity? FindEntityByName(string name) => Entity.FindByName(name);

    protected T? GetComponent<T>() where T : Component, new() => Entity.GetComponent<T>();
    protected T? AddComponent<T>() where T : Component, new() => Entity.AddComponent<T>(); 

    protected Entity Create(string name = "") => Entity.Create(name);
    protected Entity Create(Entity source) => Entity.Create(source);
    protected Entity Instantiate(Entity prefabOrSource) => Entity.Instantiate(prefabOrSource);

    public virtual void OnAwake() { }
    public virtual void OnStart() { }
    public virtual void OnUpdate() { }
    public virtual void OnDestroy() { }

    public virtual void OnApplicationStart() { }
    public virtual void OnApplicationPaused() { }
    public virtual void OnApplicationQuit() { }
    public virtual void OnFocusChanged(bool focused) { }
    public virtual void OnLogMessage(string message) { }

    public virtual void OnKeyDown(KeyCode key) { }
    public virtual void OnKeyUp(KeyCode key) { }
    public virtual void OnMouseDown(MouseButton button) { }
    public virtual void OnMouseUp(MouseButton button) { }
    public virtual void OnMouseScroll(float delta) { }
    public virtual void OnMouseMove(Vector2 position) { }

    public virtual void OnBeforeSceneLoaded(Scene scene) { }
    public virtual void OnSceneLoaded(Scene scene) { }
    public virtual void OnBeforeSceneUnloaded(Scene scene) { }
    public virtual void OnSceneUnloaded(Scene scene) { }

    public virtual void OnEnable()  { }
    public virtual void OnDisable() { }

    public virtual void OnCollisionEnter2D(Collision2D collision) { }
    public virtual void OnCollisionStay2D(Collision2D collision) { }
    public virtual void OnCollisionExit2D(Collision2D collision) { }
}

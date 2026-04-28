namespace Bolt
{
    /// <summary>
    /// Base class for user C# scripts. Subclass and implement Start()/Update()/OnDestroy().
    /// </summary>
    public abstract class BoltScript
    {
        public Entity Entity { get; internal set; } = Entity.Invalid;

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
    }
}

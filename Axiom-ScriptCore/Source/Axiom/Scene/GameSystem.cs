namespace Axiom;

public abstract class GameSystem
{
    public Scene Scene { get; private set; } = new();

    internal void _SetSceneName(string sceneName)
    {
        Scene = new Scene { Name = sceneName };
    }

    public QueryBuilder<TComponent> Query<TComponent>()
        where TComponent : Component, new()
        => Scene.Query<TComponent>();

    public QueryBuilder<T1, T2> Query<T1, T2>()
        where T1 : Component, new()
        where T2 : Component, new()
        => Scene.Query<T1, T2>();

    public QueryBuilder<T1, T2, T3> Query<T1, T2, T3>()
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        => Scene.Query<T1, T2, T3>();

    public QueryBuilder<T1, T2, T3, T4> Query<T1, T2, T3, T4>()
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        where T4 : Component, new()
        => Scene.Query<T1, T2, T3, T4>();

    public QueryBuilder<T1, T2, T3, T4, T5> Query<T1, T2, T3, T4, T5>()
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        where T4 : Component, new()
        where T5 : Component, new()
        => Scene.Query<T1, T2, T3, T4, T5>();

    public QueryBuilder<T1, T2, T3, T4, T5, T6> Query<T1, T2, T3, T4, T5, T6>()
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        where T4 : Component, new()
        where T5 : Component, new()
        where T6 : Component, new()
        => Scene.Query<T1, T2, T3, T4, T5, T6>();

    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7> Query<T1, T2, T3, T4, T5, T6, T7>()
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        where T4 : Component, new()
        where T5 : Component, new()
        where T6 : Component, new()
        where T7 : Component, new()
        => Scene.Query<T1, T2, T3, T4, T5, T6, T7>();

    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8> Query<T1, T2, T3, T4, T5, T6, T7, T8>()
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        where T4 : Component, new()
        where T5 : Component, new()
        where T6 : Component, new()
        where T7 : Component, new()
        where T8 : Component, new()
        => Scene.Query<T1, T2, T3, T4, T5, T6, T7, T8>();


    public virtual void OnAwake() { }
    public virtual void OnStart() { }
    public virtual void OnUpdate() { }
    public virtual void OnDestroy() { }

    public virtual void OnEnable() { }
    public virtual void OnDisable() { }

    public virtual void OnApplicationPaused() { }
    public virtual void OnApplicationQuit() { }
    public virtual void OnFocusChanged(bool focused) { }
}

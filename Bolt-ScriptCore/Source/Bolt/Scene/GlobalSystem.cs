namespace Bolt;

public abstract class GlobalSystem
{
    public virtual void OnInitialize() { }
    public virtual void OnUpdate() { }
    public virtual void OnEnable() { }
    public virtual void OnDisable() { }

    public virtual void OnApplicationPaused() { }
    public virtual void OnApplicationQuit() { }
    public virtual void OnFocusChanged(bool focused) { }

}

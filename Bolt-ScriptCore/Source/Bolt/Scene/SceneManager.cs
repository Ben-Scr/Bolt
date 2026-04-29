using System;
using System.Threading.Tasks;

namespace Bolt;
public enum LoadSceneMode { Single, Additive }

public static class SceneManager
{
    public static event Action<Scene>? SceneLoaded;
    public static event Action<Scene>? BeforeSceneLoaded;
    public static event Action<Scene>? BeforeSceneUnloaded;
    public static event Action<Scene>? SceneUnloaded;

    internal static void RaiseBeforeSceneLoaded(string name)
    {
        if (!string.IsNullOrEmpty(name))
            BeforeSceneLoaded?.Invoke(new Scene { Name = name });
    }

    internal static void RaiseSceneLoaded(string name)
    {
        if (!string.IsNullOrEmpty(name))
            SceneLoaded?.Invoke(new Scene { Name = name });
    }

    internal static void RaiseBeforeSceneUnloaded(string name)
    {
        if (!string.IsNullOrEmpty(name))
            BeforeSceneUnloaded?.Invoke(new Scene { Name = name });
    }

    internal static void RaiseSceneUnloaded(string name)
    {
        if (!string.IsNullOrEmpty(name))
            SceneUnloaded?.Invoke(new Scene { Name = name });
    }

    /// <summary>
    /// Returns the currently active scene.
    /// </summary>
    public static Scene? GetActiveScene()
    {
        string name = InternalCalls.Scene_GetActiveSceneName();
        if (string.IsNullOrEmpty(name)) return null;
        return new Scene { Name = name };
    }

    /// <summary>
    /// Loads a scene by name. In Single mode, all non-persistent scenes
    /// are unloaded first. In Additive mode, the scene is loaded
    /// alongside existing scenes.
    /// </summary>
    public static Scene? LoadScene(string name, LoadSceneMode mode = LoadSceneMode.Single)
    {
        bool success;
        if (mode == LoadSceneMode.Additive)
            success = InternalCalls.Scene_LoadAdditive(name);
        else
            success = InternalCalls.Scene_Load(name);

        return success ? new Scene { Name = name } : null;
    }

    /// <summary>
    /// Async version of LoadScene.
    /// </summary>
    public static async Task<Scene?> LoadSceneAsync(string name, LoadSceneMode mode = LoadSceneMode.Single)
    {
        return await Task.FromResult(LoadScene(name, mode));
    }

    /// <summary>
    /// Unloads a scene by name. Persistent scenes are not unloaded.
    /// </summary>
    public static void UnloadScene(string name)
    {
        InternalCalls.Scene_Unload(name);
    }

    /// <summary>
    /// Async version of UnloadScene.
    /// </summary>
    public static async Task UnloadSceneAsync(string name)
    {
        UnloadScene(name);
        await Task.CompletedTask;
    }

    /// <summary>
    /// Sets the active scene by name. Returns true if successful.
    /// </summary>
    public static bool SetActiveScene(string name)
    {
        return InternalCalls.Scene_SetActive(name);
    }

    /// <summary>
    /// Returns a loaded scene by name, or null if not loaded.
    /// </summary>
    public static Scene? GetLoadedSceneByName(string name)
    {
        int count = InternalCalls.Scene_GetLoadedCount();
        for (int i = 0; i < count; i++)
        {
            string loadedName = InternalCalls.Scene_GetLoadedSceneNameAt(i);
            if (string.Equals(loadedName, name, StringComparison.OrdinalIgnoreCase))
                return new Scene { Name = loadedName };
        }
        return null;
    }

    public static bool IsSceneLoaded(string name)
    {
        return !string.IsNullOrWhiteSpace(name) && GetLoadedSceneByName(name) != null;
    }

    public static bool DoesSceneExist(string name)
    {
        return !string.IsNullOrWhiteSpace(name) && InternalCalls.Scene_DoesSceneExist(name);
    }

    public static void EnableGlobalSystem<T>() where T : GlobalSystem
        => SetGlobalSystemEnabled<T>(true);

    public static void DisableGlobalSystem<T>() where T : GlobalSystem
        => SetGlobalSystemEnabled<T>(false);

    private static void SetGlobalSystemEnabled<T>(bool enabled) where T : GlobalSystem
    {
        InternalCalls.Scene_SetGlobalSystemEnabled(typeof(T).Name, enabled);
    }

    /// <summary>
    /// Returns all currently loaded scenes.
    /// </summary>
    public static Scene[] GetLoadedScenes()
    {
        int count = InternalCalls.Scene_GetLoadedCount();
        var scenes = new Scene[count];
        for (int i = 0; i < count; i++)
            scenes[i] = new Scene { Name = InternalCalls.Scene_GetLoadedSceneNameAt(i) };
        return scenes;
    }

    /// <summary>
    /// Returns the number of currently loaded scenes.
    /// </summary>
    public static int LoadedSceneCount => InternalCalls.Scene_GetLoadedCount();

    /// <summary>
    /// Reloads a scene by name. Captures entity state, unloads, reloads, and restores.
    /// </summary>
    public static bool ReloadScene(string name)
    {
        return InternalCalls.Scene_Reload(name);
    }
}

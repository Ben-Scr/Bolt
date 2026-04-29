using System;
using System.IO;

namespace Bolt;

/// <summary>
/// Provides static access to application-level runtime information
/// such as timing, screen dimensions, and lifecycle control.
/// </summary>
public static class Application
{
    public static event Action<bool>? FocusChanged;
    public static event Action? ApplicationPaused;
    public static event Action? ApplicationStart;

    internal static void RaiseFocusChanged(bool focused) => FocusChanged?.Invoke(focused);
    internal static void RaiseApplicationPaused() => ApplicationPaused?.Invoke();
    internal static void RaiseApplicationStart() => ApplicationStart?.Invoke();

    public static float TargetFrameRate
    {
        get => InternalCalls.Application_GetTargetFrameRate();
        set => InternalCalls.Application_SetTargetFrameRate(value);
    }

    public static bool VsyncEnabled
    {
        get => throw new System.NotImplementedException();
        set => throw new System.NotImplementedException();
    }

    /// <summary>
    /// The path to the persistent data directory of the application.
    /// Use this to store save files, settings, or any data that should
    /// persist across sessions. The directory is guaranteed to be writable.
    /// </summary>
    public static string PersistentDataPath => EnsureDirectory(
        Path.Combine(GetLocalApplicationDataPath(), "Bolt"));

    /// <summary>
    /// The path to the directory where the application is installed.
    /// </summary>
    public static string DataPath => AppContext.BaseDirectory;

    /// <summary>
    /// The temporary cache directory. Data here may be cleared by the OS.
    /// </summary>
    public static string TemporaryCachePath => EnsureDirectory(
        Path.Combine(Path.GetTempPath(), "Bolt"));

    public static int ScreenWidth => InternalCalls.Application_GetScreenWidth();
    public static int ScreenHeight => InternalCalls.Application_GetScreenHeight();
    public static float AspectRatio => ScreenHeight > 0 ? (float)ScreenWidth / ScreenHeight : 1.0f;

    /// <summary>
    /// Quits the application. Only works in build mode, ignored in the editor.
    /// </summary>
    public static void Quit() => InternalCalls.Application_Quit();

    private static string GetLocalApplicationDataPath()
    {
        string path = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        return string.IsNullOrEmpty(path) ? AppContext.BaseDirectory : path;
    }

    private static string EnsureDirectory(string path)
    {
        Directory.CreateDirectory(path);
        return path;
    }
}

using System;
using System.Runtime.InteropServices;

namespace Bolt.Interop;
/// <summary>
/// Layout must match the C++ ManagedCallbacks struct exactly.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ManagedCallbacksStruct
{
    public delegate* unmanaged<byte*, ulong, int> CreateScriptInstance;
    public delegate* unmanaged<int, void> DestroyScriptInstance;
    public delegate* unmanaged<int, void> InvokeStart;
    public delegate* unmanaged<int, void> InvokeUpdate;
    public delegate* unmanaged<int, void> InvokeOnDestroy;
    public delegate* unmanaged<int, void> InvokeOnEnable;
    public delegate* unmanaged<int, void> InvokeOnDisable;
    public delegate* unmanaged<int, ulong, ulong, ulong, ulong, void> InvokeCollisionEnter2D;
    public delegate* unmanaged<int, ulong, ulong, ulong, ulong, void> InvokeCollisionExit2D;
    public delegate* unmanaged<byte*, int> ClassExists;
    public delegate* unmanaged<byte*, int> LoadUserAssembly;
    public delegate* unmanaged<void> UnloadUserAssembly;
    public delegate* unmanaged<int, byte*> GetScriptFields;
    public delegate* unmanaged<int, byte*, byte*, void> SetScriptField;
    public delegate* unmanaged<byte*, byte*> GetClassFieldDefs;
    public delegate* unmanaged<void> RaiseApplicationStart;
    public delegate* unmanaged<void> RaiseApplicationPaused;
    public delegate* unmanaged<int, void> RaiseFocusChanged;
    public delegate* unmanaged<int, void> RaiseKeyDown;
    public delegate* unmanaged<int, void> RaiseKeyUp;
    public delegate* unmanaged<int, void> RaiseMouseDown;
    public delegate* unmanaged<int, void> RaiseMouseUp;
    public delegate* unmanaged<float, void> RaiseMouseScroll;
    public delegate* unmanaged<float, float, void> RaiseMouseMove;
    public delegate* unmanaged<byte*, void> RaiseBeforeSceneLoaded;
    public delegate* unmanaged<byte*, void> RaiseSceneLoaded;
    public delegate* unmanaged<byte*, void> RaiseBeforeSceneUnloaded;
    public delegate* unmanaged<byte*, void> RaiseSceneUnloaded;
    public delegate* unmanaged<byte*, byte*, int> CreateGameSystemInstance;
    public delegate* unmanaged<int, void> DestroyGameSystemInstance;
    public delegate* unmanaged<int, void> InvokeGameSystemStart;
    public delegate* unmanaged<int, void> InvokeGameSystemUpdate;
    public delegate* unmanaged<int, void> InvokeGameSystemDestroy;
    public delegate* unmanaged<byte*, int> GameSystemClassExists;
    public delegate* unmanaged<byte*, int> CreateGlobalSystemInstance;
    public delegate* unmanaged<int, void> DestroyGlobalSystemInstance;
    public delegate* unmanaged<int, void> InvokeGlobalSystemInitialize;
    public delegate* unmanaged<int, void> InvokeGlobalSystemUpdate;
    public delegate* unmanaged<byte*, int> GlobalSystemClassExists;
}

/// <summary>
/// Entry point called from C++ via hostfxr load_assembly_and_get_function_pointer.
/// Exchanges native ↔ managed function pointers. Returns 0 on success.
/// </summary>
internal static class ScriptHostBridge
{
    [UnmanagedCallersOnly]
    internal static unsafe int Initialize(
        NativeBindingsStruct* nativeBindings,
        ManagedCallbacksStruct* managedCallbacks)
    {
        try
        {
            NativeCallbacks.SetFrom(nativeBindings);

            managedCallbacks->CreateScriptInstance = &ScriptInstanceManager.CreateScriptInstance;
            managedCallbacks->DestroyScriptInstance = &ScriptInstanceManager.DestroyScriptInstance;
            managedCallbacks->InvokeStart = &ScriptInstanceManager.InvokeStart;
            managedCallbacks->InvokeUpdate = &ScriptInstanceManager.InvokeUpdate;
            managedCallbacks->InvokeOnDestroy = &ScriptInstanceManager.InvokeOnDestroy;
            managedCallbacks->InvokeOnEnable = &ScriptInstanceManager.InvokeOnEnable;
            managedCallbacks->InvokeOnDisable = &ScriptInstanceManager.InvokeOnDisable;
            managedCallbacks->InvokeCollisionEnter2D = &ScriptInstanceManager.InvokeCollisionEnter2D;
            managedCallbacks->InvokeCollisionExit2D = &ScriptInstanceManager.InvokeCollisionExit2D;
            managedCallbacks->ClassExists = &ScriptInstanceManager.ClassExists;
            managedCallbacks->LoadUserAssembly = &ScriptInstanceManager.LoadUserAssembly;
            managedCallbacks->UnloadUserAssembly = &ScriptInstanceManager.UnloadUserAssembly;
            managedCallbacks->GetScriptFields = &ScriptInstanceManager.GetScriptFields;
            managedCallbacks->SetScriptField = &ScriptInstanceManager.SetScriptField;
            managedCallbacks->GetClassFieldDefs = &ScriptInstanceManager.GetClassFieldDefs;
            managedCallbacks->RaiseApplicationStart = &ScriptInstanceManager.RaiseApplicationStart;
            managedCallbacks->RaiseApplicationPaused = &ScriptInstanceManager.RaiseApplicationPaused;
            managedCallbacks->RaiseFocusChanged = &ScriptInstanceManager.RaiseFocusChanged;
            managedCallbacks->RaiseKeyDown = &ScriptInstanceManager.RaiseKeyDown;
            managedCallbacks->RaiseKeyUp = &ScriptInstanceManager.RaiseKeyUp;
            managedCallbacks->RaiseMouseDown = &ScriptInstanceManager.RaiseMouseDown;
            managedCallbacks->RaiseMouseUp = &ScriptInstanceManager.RaiseMouseUp;
            managedCallbacks->RaiseMouseScroll = &ScriptInstanceManager.RaiseMouseScroll;
            managedCallbacks->RaiseMouseMove = &ScriptInstanceManager.RaiseMouseMove;
            managedCallbacks->RaiseBeforeSceneLoaded = &ScriptInstanceManager.RaiseBeforeSceneLoaded;
            managedCallbacks->RaiseSceneLoaded = &ScriptInstanceManager.RaiseSceneLoaded;
            managedCallbacks->RaiseBeforeSceneUnloaded = &ScriptInstanceManager.RaiseBeforeSceneUnloaded;
            managedCallbacks->RaiseSceneUnloaded = &ScriptInstanceManager.RaiseSceneUnloaded;
            managedCallbacks->CreateGameSystemInstance = &ScriptInstanceManager.CreateGameSystemInstance;
            managedCallbacks->DestroyGameSystemInstance = &ScriptInstanceManager.DestroyGameSystemInstance;
            managedCallbacks->InvokeGameSystemStart = &ScriptInstanceManager.InvokeGameSystemStart;
            managedCallbacks->InvokeGameSystemUpdate = &ScriptInstanceManager.InvokeGameSystemUpdate;
            managedCallbacks->InvokeGameSystemDestroy = &ScriptInstanceManager.InvokeGameSystemDestroy;
            managedCallbacks->GameSystemClassExists = &ScriptInstanceManager.GameSystemClassExists;
            managedCallbacks->CreateGlobalSystemInstance = &ScriptInstanceManager.CreateGlobalSystemInstance;
            managedCallbacks->DestroyGlobalSystemInstance = &ScriptInstanceManager.DestroyGlobalSystemInstance;
            managedCallbacks->InvokeGlobalSystemInitialize = &ScriptInstanceManager.InvokeGlobalSystemInitialize;
            managedCallbacks->InvokeGlobalSystemUpdate = &ScriptInstanceManager.InvokeGlobalSystemUpdate;
            managedCallbacks->GlobalSystemClassExists = &ScriptInstanceManager.GlobalSystemClassExists;

            ScriptInstanceManager.SetCoreAssembly(typeof(ScriptHostBridge).Assembly);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"ScriptHostBridge.Initialize failed: {ex}");
            return -1;
        }
    }
}

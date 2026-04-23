using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using Bolt;

namespace Bolt.Hosting
{
    /// <summary>
    /// Resolves Bolt-ScriptCore from the default context to prevent duplicate type loading.
    /// </summary>
    internal class ScriptAssemblyLoadContext : AssemblyLoadContext
    {
        private readonly Assembly _coreAssembly;
        private readonly AssemblyDependencyResolver? _resolver;
        private readonly string? _userAssemblyDir;

        public ScriptAssemblyLoadContext(Assembly coreAssembly, string? userAssemblyPath = null)
            : base("BoltUserScripts", isCollectible: true)
        {
            _coreAssembly = coreAssembly;
            if (userAssemblyPath != null)
            {
                _userAssemblyDir = System.IO.Path.GetDirectoryName(
                    System.IO.Path.GetFullPath(userAssemblyPath));

                try { _resolver = new AssemblyDependencyResolver(userAssemblyPath); }
                catch (Exception ex)
                {
                    Log.Warn($"[ScriptLoader] AssemblyDependencyResolver failed: {ex.Message}");
                    _resolver = null;
                }
            }

            // Register Resolving event as a last-resort fallback.
            // This fires when Load() returns null AND the Default context also fails.
            this.Resolving += OnResolving;
        }

        protected override Assembly? Load(AssemblyName name)
        {
            if (name.Name == _coreAssembly.GetName().Name)
                return _coreAssembly;

            // Try .deps.json resolver (resolves NuGet packages from cache or local copy)
            if (_resolver != null)
            {
                string? resolvedPath = _resolver.ResolveAssemblyToPath(name);
                if (resolvedPath != null)
                    return LoadFromAssemblyPath(resolvedPath);
            }

            // Probe the user assembly's output directory
            Assembly? probed = ProbeDirectory(_userAssemblyDir, name.Name);
            if (probed != null) return probed;

            return null;
        }

        private Assembly? OnResolving(AssemblyLoadContext context, AssemblyName name)
        {
            // Last-resort: re-probe directory and NuGet cache
            Log.Warn($"[ScriptLoader] Resolving fallback for: {name.Name}");

            Assembly? probed = ProbeDirectory(_userAssemblyDir, name.Name);
            if (probed != null) return probed;

            // Check NuGet global packages cache
            if (name.Name != null && name.Version != null)
            {
                string? nugetDir = System.Environment.GetEnvironmentVariable("NUGET_PACKAGES");
                if (string.IsNullOrEmpty(nugetDir))
                    nugetDir = System.IO.Path.Combine(
                        System.Environment.GetFolderPath(System.Environment.SpecialFolder.UserProfile),
                        ".nuget", "packages");

                string packageDir = System.IO.Path.Combine(
                    nugetDir, name.Name.ToLowerInvariant(), name.Version.ToString());

                if (System.IO.Directory.Exists(packageDir))
                {
                    string[] tfms = { "net9.0", "net8.0", "net7.0", "net6.0", "netstandard2.1", "netstandard2.0" };
                    foreach (string tfm in tfms)
                    {
                        string dllPath = System.IO.Path.Combine(packageDir, "lib", tfm, name.Name + ".dll");
                        if (System.IO.File.Exists(dllPath))
                            return LoadFromAssemblyPath(dllPath);
                    }
                }
            }

            Log.Error($"[ScriptLoader] Failed to resolve: {name.FullName}");
            return null;
        }

        private Assembly? ProbeDirectory(string? dir, string? assemblyName)
        {
            if (dir == null || assemblyName == null) return null;
            string candidate = System.IO.Path.Combine(dir, assemblyName + ".dll");
            if (System.IO.File.Exists(candidate))
                return LoadFromAssemblyPath(candidate);
            return null;
        }
    }

    /// <summary>
    /// Manages script instances on the managed side. All public methods are
    /// [UnmanagedCallersOnly] and called from C++ via function pointers.
    /// </summary>
    internal static class ScriptInstanceManager
    {
        private static readonly Dictionary<int, ScriptInstanceData> s_Instances = new();
        private static int s_NextHandle = 1;

        private static Assembly? s_CoreAssembly;
        private static Assembly? s_UserAssembly;
        private static AssemblyLoadContext? s_UserLoadContext;
        private static readonly Dictionary<string, ScriptClassInfo?> s_ClassCache = new();

        private struct ScriptInstanceData
        {
            public BoltScript Instance;
            public MethodInfo? StartMethod;
            public MethodInfo? UpdateMethod;
            public MethodInfo? OnDestroyMethod;
            public bool HasStarted;
        }

        private class ScriptClassInfo
        {
            public Type Type = null!;
            public MethodInfo? StartMethod;
            public MethodInfo? UpdateMethod;
            public MethodInfo? OnDestroyMethod;
        }

        internal static void SetCoreAssembly(Assembly assembly)
        {
            s_CoreAssembly = assembly;
        }

        private static void UnloadCurrentUserAssemblyContext()
        {
            if (s_UserLoadContext == null)
                return;

            var weakContext = new WeakReference(s_UserLoadContext, trackResurrection: false);
            s_UserLoadContext.Unload();
            s_UserLoadContext = null;
            s_UserAssembly = null;
            ReleaseFieldJsonBuffer();

            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();

            if (weakContext.IsAlive)
                Log.Warn("[ScriptLoader] User assembly load context is still alive after unload; lingering references may delay full cleanup.");
        }

        [UnmanagedCallersOnly]
        public static unsafe int CreateScriptInstance(byte* classNamePtr, ulong entityID)
        {
            try
            {
                string className = Marshal.PtrToStringUTF8((IntPtr)classNamePtr)!;
                var classInfo = GetOrCacheClass(className);
                if (classInfo == null) return 0;

                var instance = (BoltScript)Activator.CreateInstance(classInfo.Type)!;
                instance._SetEntityID(entityID);

                int handle = s_NextHandle++;
                s_Instances[handle] = new ScriptInstanceData
                {
                    Instance = instance,
                    StartMethod = classInfo.StartMethod,
                    UpdateMethod = classInfo.UpdateMethod,
                    OnDestroyMethod = classInfo.OnDestroyMethod,
                    HasStarted = false
                };

                return handle;
            }
            catch (Exception ex)
            {
                Log.Error($"CreateScriptInstance failed: {ex}");
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static void DestroyScriptInstance(int handle) => s_Instances.Remove(handle);

        [UnmanagedCallersOnly]
        public static void InvokeStart(int handle)
        {
            if (!s_Instances.TryGetValue(handle, out var data) || data.HasStarted) return;

            try
            {
                data.StartMethod?.Invoke(data.Instance, null);
                data.HasStarted = true;
                s_Instances[handle] = data;
            }
            catch (TargetInvocationException ex) { Log.Error($"Exception in Start(): {ex.InnerException}"); }
        }

        [UnmanagedCallersOnly]
        public static void InvokeUpdate(int handle)
        {
            if (!s_Instances.TryGetValue(handle, out var data)) return;
            try { data.UpdateMethod?.Invoke(data.Instance, null); }
            catch (TargetInvocationException ex) { Log.Error($"Exception in Update(): {ex.InnerException}"); }
        }

        [UnmanagedCallersOnly]
        public static void InvokeOnDestroy(int handle)
        {
            if (!s_Instances.TryGetValue(handle, out var data)) return;
            try { data.OnDestroyMethod?.Invoke(data.Instance, null); }
            catch (TargetInvocationException ex) { Log.Error($"Exception in OnDestroy(): {ex.InnerException}"); }
        }

        [UnmanagedCallersOnly]
        public static unsafe int ClassExists(byte* classNamePtr)
        {
            string className = Marshal.PtrToStringUTF8((IntPtr)classNamePtr)!;
            return GetOrCacheClass(className) != null ? 1 : 0;
        }

        [UnmanagedCallersOnly]
        public static unsafe int LoadUserAssembly(byte* pathPtr)
        {
            try
            {
                string path = Marshal.PtrToStringUTF8((IntPtr)pathPtr)!;

                if (s_UserLoadContext != null)
                {
                    s_Instances.Clear();
                    s_ClassCache.Clear();
                    UnloadCurrentUserAssemblyContext();
                }

                s_ClassCache.Clear();

                // Load from bytes to avoid file-locking the DLL on disk
                string fullPath = System.IO.Path.GetFullPath(path);
                s_UserLoadContext = new ScriptAssemblyLoadContext(s_CoreAssembly!, fullPath);
                byte[] assemblyBytes = System.IO.File.ReadAllBytes(fullPath);
                s_UserAssembly = s_UserLoadContext.LoadFromStream(
                    new System.IO.MemoryStream(assemblyBytes));

                Log.Info($"User assembly loaded: {path}");
                return 1;
            }
            catch (Exception ex)
            {
                Log.Error($"Failed to load user assembly: {ex.Message}");
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static void UnloadUserAssembly()
        {
            s_Instances.Clear();
            s_ClassCache.Clear();
            ReleaseFieldJsonBuffer();

            UnloadCurrentUserAssemblyContext();
        }

        // ── Field reflection for [ShowInEditor] ──────────────────────

        private static readonly object s_FieldJsonBufferLock = new();
        private static byte[] s_FieldJsonBuffer = Array.Empty<byte>();
        private static GCHandle s_FieldJsonBufferHandle;

        [UnmanagedCallersOnly]
        public static unsafe byte* GetScriptFields(int handle)
        {
            try
            {
                if (!s_Instances.TryGetValue(handle, out var data))
                    return NullTerminated("[]");

                var instance = data.Instance;
                var type = instance.GetType();
                var fields = type.GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);

                var sb = new System.Text.StringBuilder();
                sb.Append('[');
                bool first = true;

                foreach (var field in fields)
                {
                    // Skip fields from BoltScript base class
                    if (field.DeclaringType == typeof(BoltScript)) continue;

                    var showAttr = field.GetCustomAttribute<ShowInEditorAttribute>();
                    if (showAttr == null) continue;

                    string fieldType = MapFieldType(field.FieldType);
                    if (fieldType == "unsupported") continue;

                    object? val = field.GetValue(instance);
                    string valueStr = FormatFieldValue(field.FieldType, val);

                    string displayName = string.IsNullOrEmpty(showAttr.DisplayName) ? field.Name : showAttr.DisplayName;
                    bool readOnly = showAttr.ReadOnly;

                    float clampMin = 0, clampMax = 0;
                    bool hasClamp = false;
                    var clampAttr = field.GetCustomAttribute<ClampValueAttribute>();
                    if (clampAttr != null)
                    {
                        clampMin = clampAttr.Min;
                        clampMax = clampAttr.Max;
                        hasClamp = true;
                    }

                    string tooltip = "";
                    var tooltipAttr = field.GetCustomAttribute<ToolTipAttribute>();
                    if (tooltipAttr != null)
                        tooltip = tooltipAttr.Text;

                    if (!first) sb.Append(',');
                    first = false;

                    sb.Append("{\"name\":\"").Append(EscapeJson(field.Name))
                      .Append("\",\"displayName\":\"").Append(EscapeJson(displayName))
                      .Append("\",\"type\":\"").Append(fieldType)
                      .Append("\",\"value\":\"").Append(EscapeJson(valueStr))
                      .Append("\",\"readOnly\":").Append(readOnly ? "true" : "false")
                      .Append(",\"hasClamp\":").Append(hasClamp ? "true" : "false")
                      .Append(",\"clampMin\":").Append(clampMin.ToString(System.Globalization.CultureInfo.InvariantCulture))
                      .Append(",\"clampMax\":").Append(clampMax.ToString(System.Globalization.CultureInfo.InvariantCulture))
                      .Append(",\"tooltip\":\"").Append(EscapeJson(tooltip))
                      .Append("\"}");
                }

                sb.Append(']');
                return NullTerminated(sb.ToString());
            }
            catch (Exception ex)
            {
                Log.Error($"GetScriptFields failed: {ex.Message}");
                return NullTerminated("[]");
            }
        }

        [UnmanagedCallersOnly]
        public static unsafe void SetScriptField(int handle, byte* fieldNamePtr, byte* valuePtr)
        {
            try
            {
                if (!s_Instances.TryGetValue(handle, out var data)) return;

                string fieldName = Marshal.PtrToStringUTF8((IntPtr)fieldNamePtr) ?? "";
                string valueStr = Marshal.PtrToStringUTF8((IntPtr)valuePtr) ?? "";

                var instance = data.Instance;
                var field = instance.GetType().GetField(fieldName,
                    BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);
                if (field == null) return;

                object? parsed = ParseFieldValue(field.FieldType, valueStr);
                if (parsed != null)
                    field.SetValue(instance, parsed);
            }
            catch (Exception ex)
            {
                Log.Error($"SetScriptField failed: {ex.Message}");
            }
        }

        private static string MapFieldType(Type t)
        {
            // Primitve types
            if (t == typeof(float)) return "float";
            if (t == typeof(double)) return "double";
            if (t == typeof(int)) return "int";
            if (t == typeof(short)) return "short";
            if (t == typeof(byte)) return "byte";
            if (t == typeof(long)) return "long";
            if (t == typeof(uint)) return "uint";
            if (t == typeof(ushort)) return "ushort";
            if (t == typeof(sbyte)) return "sbyte";
            if (t == typeof(ulong)) return "ulong";
            if (t == typeof(bool)) return "bool";
            if (t == typeof(string)) return "string";

            // Bolt-specific types
            if (t == typeof(Color)) return "color";
            if (t == typeof(Entity)) return "entity";
            if (t == typeof(TextureRef)) return "texture";
            if (t == typeof(AudioRef)) return "audio";
            if(t == typeof(Vector2)) return "vector2";
            if (t == typeof(Vector2Int)) return "vector2Int";
            if (t == typeof(Vector3)) return "vector3";
            if (t == typeof(Vector3Int)) return "vector3Int";
            if (t == typeof(Vector4)) return "vector4";
            if (t == typeof(Vector4Int)) return "vector4Int";

            if (t.IsSubclassOf(typeof(Component)))
            {
                return Entity.TryGetNativeComponentName(t, out string? nativeName)
                    ? "component:" + nativeName
                    : "unsupported";
            }
            return "unsupported";
        }

        private static string FormatFieldValue(Type t, object? val)
        {
            if (val == null) return "";

            //INFO(Ben-Scr): This may causes bugs when sharing projects across different locales
            var ic = System.Globalization.CultureInfo.InvariantCulture;

            // Primitve types
            if (t == typeof(float)) return ((float)val).ToString(ic);
            if (t == typeof(double)) return ((double)val).ToString(ic);
            if (t == typeof(int)) return ((int)val).ToString(ic);
            if (t == typeof(short)) return ((short)val).ToString(ic);
            if (t == typeof(byte)) return ((byte)val).ToString(ic);
            if (t == typeof(long)) return ((long)val).ToString(ic);
            if (t == typeof(uint)) return ((uint)val).ToString(ic);
            if (t == typeof(ushort)) return ((ushort)val).ToString(ic);
            if (t == typeof(sbyte)) return ((sbyte)val).ToString(ic);
            if (t == typeof(ulong)) return ((ulong)val).ToString(ic);
            if (t == typeof(bool)) return (bool)val ? "true" : "false";
            if (t == typeof(string)) return (string)val;


            if (t == typeof(Color))
            {
                var c = (Color)val;
                return $"{c.R.ToString(ic)},{c.G.ToString(ic)},{c.B.ToString(ic)},{c.A.ToString(ic)}";
            }
            if (t == typeof(Entity))
            {
                var entity = (Entity)val;
                if (entity == null || entity == Entity.Invalid) return "0";

                if (entity.HasComponent<Transform2DComponent>())
                {
                    return entity.ID.ToString(ic);
                }
                return entity.ID.ToString(ic);
            }
            if (t == typeof(TextureRef))
            {
                TextureRef assetRef = (TextureRef)val;
                return assetRef.UUID != 0 ? assetRef.UUID.ToString(ic) : "";
            }
            if (t == typeof(AudioRef))
            {
                AudioRef assetRef = (AudioRef)val;
                return assetRef.UUID != 0 ? assetRef.UUID.ToString(ic) : "";
            }
            if(t == typeof(Vector2))
            {
                var v = (Vector2)val;
                return $"{v.X.ToString(ic)},{v.Y.ToString(ic)}";
            }
            if (t == typeof(Vector2Int))
            {
                var v = (Vector2Int)val;
                return $"{v.X.ToString(ic)},{v.Y.ToString(ic)}";
            }
            if (t == typeof(Vector3))
            {
                var v = (Vector3)val;
                return $"{v.X.ToString(ic)},{v.Y.ToString(ic)},{v.Z.ToString(ic)}";
            }
            if (t == typeof(Vector3Int))
            {
                var v = (Vector3Int)val;
                return $"{v.X.ToString(ic)},{v.Y.ToString(ic)},{v.Z.ToString(ic)}";
            }
            if (t == typeof(Vector4))
            {
                var v = (Vector4)val;
                return $"{v.X.ToString(ic)},{v.Y.ToString(ic)},{v.Z.ToString(ic)},{v.W.ToString(ic)}";
            }
            if (t == typeof(Vector4Int))
            {
                var v = (Vector4Int)val;
                return $"{v.X.ToString(ic)},{v.Y.ToString(ic)},{v.Z.ToString(ic)},{v.W.ToString(ic)}";
            }

            if (t.IsSubclassOf(typeof(Component)))
            {
                var comp = (Component)val;
                if (comp?.Entity == null || comp.Entity == Entity.Invalid) return "";
                string typeName = Entity.TryGetNativeComponentName(t, out string? nativeName)
                    ? nativeName!
                    : t.Name;
                return comp.Entity.ID.ToString(ic) + ":" + typeName;
            }
            return val.ToString() ?? "";
        }

        private static ulong ParseAssetUUID(string value)
        {
            if (string.IsNullOrWhiteSpace(value))
                return 0;

            if (ulong.TryParse(value, System.Globalization.NumberStyles.None,
                System.Globalization.CultureInfo.InvariantCulture, out ulong assetId))
            {
                return assetId;
            }

            return InternalCalls.Asset_GetOrCreateUUIDFromPath(value);
        }

        private static bool MatchesComponentReferenceType(Type componentType, string serializedTypeName)
        {
            if (Entity.TryGetNativeComponentName(componentType, out string? nativeName)
                && string.Equals(nativeName, serializedTypeName, StringComparison.Ordinal))
            {
                return true;
            }

            return string.Equals(componentType.Name, serializedTypeName, StringComparison.Ordinal);
        }

        private static object? ParseFieldValue(Type t, string s)
        {
            var ic = System.Globalization.CultureInfo.InvariantCulture;
            try
            {
                if (t == typeof(float)) return float.Parse(s, ic);
                if (t == typeof(double)) return double.Parse(s, ic);
                if (t == typeof(int)) return int.Parse(s, ic);
                if (t == typeof(short)) return short.Parse(s, ic);
                if (t == typeof(byte)) return byte.Parse(s, ic);
                if (t == typeof(long)) return long.Parse(s, ic);
                if (t == typeof(uint)) return uint.Parse(s, ic);
                if (t == typeof(ushort)) return ushort.Parse(s, ic);
                if (t == typeof(sbyte)) return sbyte.Parse(s, ic);
                if (t == typeof(ulong)) return ulong.Parse(s, ic);
                if (t == typeof(bool)) return s == "true" || s == "True" || s == "1";
                if (t == typeof(string)) return s;
                if (t == typeof(Color))
                {
                    var parts = s.Split(',');
                    if (parts.Length >= 4)
                        return new Color(
                            float.Parse(parts[0], ic), float.Parse(parts[1], ic),
                            float.Parse(parts[2], ic), float.Parse(parts[3], ic));
                    return new Color(1, 1, 1, 1);
                }
                if (t == typeof(Vector2))
                {
                    var parts = s.Split(',', StringSplitOptions.TrimEntries);
                    if (parts.Length >= 2)
                        return new Vector2(
                            float.Parse(parts[0], ic),
                            float.Parse(parts[1], ic));
                    return null;
                }
                if (t == typeof(Vector2Int))
                {
                    var parts = s.Split(',', StringSplitOptions.TrimEntries);
                    if (parts.Length >= 2)
                        return new Vector2Int(
                            int.Parse(parts[0], ic),
                            int.Parse(parts[1], ic));
                    return null;
                }
                if (t == typeof(Vector3))
                {
                    var parts = s.Split(',', StringSplitOptions.TrimEntries);
                    if (parts.Length >= 3)
                        return new Vector3(
                            float.Parse(parts[0], ic),
                            float.Parse(parts[1], ic),
                            float.Parse(parts[2], ic));
                    return null;
                }
                if (t == typeof(Vector3Int))
                {
                    var parts = s.Split(',', StringSplitOptions.TrimEntries);
                    if (parts.Length >= 3)
                        return new Vector3Int(
                            int.Parse(parts[0], ic),
                            int.Parse(parts[1], ic),
                            int.Parse(parts[2], ic));
                    return null;
                }
                if (t == typeof(Vector4))
                {
                    var parts = s.Split(',', StringSplitOptions.TrimEntries);
                    if (parts.Length >= 4)
                        return new Vector4(
                            float.Parse(parts[0], ic),
                            float.Parse(parts[1], ic),
                            float.Parse(parts[2], ic),
                            float.Parse(parts[3], ic));
                    return null;
                }
                if (t == typeof(Vector4Int))
                {
                    var parts = s.Split(',', StringSplitOptions.TrimEntries);
                    if (parts.Length >= 4)
                        return new Vector4Int(
                            int.Parse(parts[0], ic),
                            int.Parse(parts[1], ic),
                            int.Parse(parts[2], ic),
                            int.Parse(parts[3], ic));
                    return null;
                }
                if (t == typeof(Entity))
                {
                    ulong id = ulong.Parse(s, ic);
                    return id != 0 ? new Entity(id) : Entity.Invalid;
                }
                if (t == typeof(TextureRef)) return new TextureRef(ParseAssetUUID(s));
                if (t == typeof(AudioRef)) return new AudioRef(ParseAssetUUID(s));
                if (t.IsSubclassOf(typeof(Component)))
                {
                    // Format: "EntityID:ComponentName"
                    var sep = s.IndexOf(':');
                    if (sep > 0)
                    {
                        ulong entityId = ulong.Parse(s.Substring(0, sep), ic);
                        string componentTypeName = s.Substring(sep + 1);
                        if (!MatchesComponentReferenceType(t, componentTypeName))
                            return null;

                        if (entityId != 0)
                        {
                            var entity = new Entity(entityId);
                            var comp = (Component)Activator.CreateInstance(t)!;
                            comp.Entity = entity;
                            return comp;
                        }
                    }
                    return null;
                }
            }
            catch { }
            return null;
        }

        private static string EscapeJson(string s)
        {
            return s.Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\n", "\\n").Replace("\r", "\\r");
        }

        private static void ReleaseFieldJsonBuffer()
        {
            lock (s_FieldJsonBufferLock)
            {
                if (s_FieldJsonBufferHandle.IsAllocated)
                    s_FieldJsonBufferHandle.Free();

                s_FieldJsonBuffer = Array.Empty<byte>();
            }
        }

        private static unsafe byte* NullTerminated(string s)
        {
            lock (s_FieldJsonBufferLock)
            {
                int byteCount = System.Text.Encoding.UTF8.GetByteCount(s);
                if (s_FieldJsonBuffer.Length < byteCount + 1)
                {
                    if (s_FieldJsonBufferHandle.IsAllocated)
                        s_FieldJsonBufferHandle.Free();

                    s_FieldJsonBuffer = new byte[byteCount + 1];
                    s_FieldJsonBufferHandle = GCHandle.Alloc(s_FieldJsonBuffer, GCHandleType.Pinned);
                }
                else if (!s_FieldJsonBufferHandle.IsAllocated)
                {
                    s_FieldJsonBufferHandle = GCHandle.Alloc(s_FieldJsonBuffer, GCHandleType.Pinned);
                }

                System.Text.Encoding.UTF8.GetBytes(s, s_FieldJsonBuffer);
                s_FieldJsonBuffer[byteCount] = 0;
                return (byte*)s_FieldJsonBufferHandle.AddrOfPinnedObject();
            }
        }

        /// <summary>
        /// Returns field definitions for a class WITHOUT needing a live instance.
        /// Creates a temporary instance to read default values, then discards it.
        /// Used by the editor in Edit Mode to show [ShowInEditor] fields.
        /// </summary>
        [UnmanagedCallersOnly]
        public static unsafe byte* GetClassFieldDefs(byte* classNamePtr)
        {
            try
            {
                string className = Marshal.PtrToStringUTF8((IntPtr)classNamePtr) ?? "";
                var classInfo = GetOrCacheClass(className);
                if (classInfo == null) return NullTerminated("[]");

                // Create a temporary instance just to read default values
                BoltScript? tempInstance = null;
                try { tempInstance = (BoltScript)Activator.CreateInstance(classInfo.Type)!; }
                catch { return NullTerminated("[]"); }

                var fields = classInfo.Type.GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);

                var sb = new System.Text.StringBuilder();
                sb.Append('[');
                bool first = true;

                foreach (var field in fields)
                {
                    if (field.DeclaringType == typeof(BoltScript)) continue;

                    var showAttr = field.GetCustomAttribute<ShowInEditorAttribute>();
                    if (showAttr == null) continue;

                    string fieldType = MapFieldType(field.FieldType);
                    if (fieldType == "unsupported") continue;

                    object? val = field.GetValue(tempInstance);
                    string valueStr = FormatFieldValue(field.FieldType, val);

                    string displayName = string.IsNullOrEmpty(showAttr.DisplayName) ? field.Name : showAttr.DisplayName;
                    bool readOnly = showAttr.ReadOnly;

                    float clampMin = 0, clampMax = 0;
                    bool hasClamp = false;
                    var clampAttr = field.GetCustomAttribute<ClampValueAttribute>();
                    if (clampAttr != null)
                    {
                        clampMin = clampAttr.Min;
                        clampMax = clampAttr.Max;
                        hasClamp = true;
                    }

                    string tooltip = "";
                    var tooltipAttr = field.GetCustomAttribute<ToolTipAttribute>();
                    if (tooltipAttr != null)
                        tooltip = tooltipAttr.Text;

                    if (!first) sb.Append(',');
                    first = false;

                    sb.Append("{\"name\":\"").Append(EscapeJson(field.Name))
                      .Append("\",\"displayName\":\"").Append(EscapeJson(displayName))
                      .Append("\",\"type\":\"").Append(fieldType)
                      .Append("\",\"value\":\"").Append(EscapeJson(valueStr))
                      .Append("\",\"readOnly\":").Append(readOnly ? "true" : "false")
                      .Append(",\"hasClamp\":").Append(hasClamp ? "true" : "false")
                      .Append(",\"clampMin\":").Append(clampMin.ToString(System.Globalization.CultureInfo.InvariantCulture))
                      .Append(",\"clampMax\":").Append(clampMax.ToString(System.Globalization.CultureInfo.InvariantCulture))
                      .Append(",\"tooltip\":\"").Append(EscapeJson(tooltip))
                      .Append("\"}");
                }

                sb.Append(']');
                return NullTerminated(sb.ToString());
            }
            catch (Exception ex)
            {
                Log.Error($"GetClassFieldDefs failed: {ex.Message}");
                return NullTerminated("[]");
            }
        }

        // ── Class cache ─────────────────────────────────────────────

        private static ScriptClassInfo? GetOrCacheClass(string className)
        {
            if (s_ClassCache.TryGetValue(className, out var cached))
                return cached;

            Type? type = FindType(className);
            if (type == null || !type.IsSubclassOf(typeof(BoltScript)))
            {
                s_ClassCache[className] = null;
                return null;
            }

            var info = new ScriptClassInfo
            {
                Type = type,
                StartMethod = type.GetMethod("Start", BindingFlags.Public | BindingFlags.Instance, Type.EmptyTypes),
                UpdateMethod = type.GetMethod("Update", BindingFlags.Public | BindingFlags.Instance, Type.EmptyTypes),
                OnDestroyMethod = type.GetMethod("OnDestroy", BindingFlags.Public | BindingFlags.Instance, Type.EmptyTypes),
            };

            s_ClassCache[className] = info;
            return info;
        }

        private static Type? FindType(string className)
        {
            if (s_UserAssembly != null)
            {
                var type = s_UserAssembly.GetType($"Bolt.{className}")
                        ?? s_UserAssembly.GetType(className);
                if (type != null) return type;
            }

            if (s_CoreAssembly != null)
            {
                var type = s_CoreAssembly.GetType($"Bolt.{className}")
                        ?? s_CoreAssembly.GetType(className);
                if (type != null) return type;
            }

            return null;
        }
    }
}

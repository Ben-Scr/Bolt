using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Axiom.Interop;
internal static unsafe class InternalCalls
{
    // ── Application ─────────────────────────────────────────────────

    internal static float Application_GetDeltaTime() => NativeCallbacks.Bindings.Application_GetDeltaTime();
    internal static float Application_GetElapsedTime() => NativeCallbacks.Bindings.Application_GetElapsedTime();
    internal static int Application_GetScreenWidth() => NativeCallbacks.Bindings.Application_GetScreenWidth();
    internal static int Application_GetScreenHeight() => NativeCallbacks.Bindings.Application_GetScreenHeight();
    internal static float Application_GetTargetFrameRate() => NativeCallbacks.Bindings.Application_GetTargetFrameRate();
    internal static void Application_SetTargetFrameRate(float fps) => NativeCallbacks.Bindings.Application_SetTargetFrameRate(fps);
    internal static void Application_Quit() => NativeCallbacks.Bindings.Application_Quit();
    internal static float Application_GetFixedDeltaTime() => NativeCallbacks.Bindings.Application_GetFixedDeltaTime();
    internal static float Application_GetUnscaledDeltaTime() => NativeCallbacks.Bindings.Application_GetUnscaledDeltaTime();
    internal static float Application_GetFixedUnscaledDeltaTime() => NativeCallbacks.Bindings.Application_GetFixedUnscaledDeltaTime();
    internal static float Application_GetTimeScale() => NativeCallbacks.Bindings.Application_GetTimeScale();
    internal static void Application_SetTimeScale(float scale) => NativeCallbacks.Bindings.Application_SetTimeScale(scale);
    internal static bool Application_IsEditor() => NativeCallbacks.Bindings.Application_IsEditor() != 0;

    // ── Log ─────────────────────────────────────────────────────────

    private static void CallStringBinding(delegate* unmanaged<byte*, void> fn, string message)
    {
        int len = Encoding.UTF8.GetByteCount(message);
        Span<byte> buf = len <= 512 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(message, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) fn(ptr);
    }

    internal static void Log_Trace(string message) => CallStringBinding(NativeCallbacks.Bindings.Log_Trace, message);
    internal static void Log_Info(string message) => CallStringBinding(NativeCallbacks.Bindings.Log_Info, message);
    internal static void Log_Warn(string message) => CallStringBinding(NativeCallbacks.Bindings.Log_Warn, message);
    internal static void Log_Error(string message) => CallStringBinding(NativeCallbacks.Bindings.Log_Error, message);

    // ── Input ───────────────────────────────────────────────────────

    internal static bool Input_GetKey(int keyCode) => NativeCallbacks.Bindings.Input_GetKey(keyCode) != 0;
    internal static bool Input_GetKeyDown(int keyCode) => NativeCallbacks.Bindings.Input_GetKeyDown(keyCode) != 0;
    internal static bool Input_GetKeyUp(int keyCode) => NativeCallbacks.Bindings.Input_GetKeyUp(keyCode) != 0;
    internal static bool Input_GetMouseButton(int button) => NativeCallbacks.Bindings.Input_GetMouseButton(button) != 0;
    internal static bool Input_GetMouseButtonDown(int button) => NativeCallbacks.Bindings.Input_GetMouseButtonDown(button) != 0;
    internal static bool Input_GetMouseButtonUp(int button) => NativeCallbacks.Bindings.Input_GetMouseButtonUp(button) != 0;

    internal static void Input_GetMousePosition(out float x, out float y)
    {
        float ox, oy;
        NativeCallbacks.Bindings.Input_GetMousePosition(&ox, &oy);
        x = ox; y = oy;
    }

    internal static void Input_GetAxis(out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.Input_GetAxis(&ox, &oy); x = ox; y = oy; }
    internal static void Input_GetMouseDelta(out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.Input_GetMouseDelta(&ox, &oy); x = ox; y = oy; }
    internal static float Input_GetScrollWheelDelta() => NativeCallbacks.Bindings.Input_GetScrollWheelDelta();

    // ── Scene ───────────────────────────────────────────────────

    internal static string Scene_GetActiveSceneName()
    {
        byte* ptr = NativeCallbacks.Bindings.Scene_GetActiveSceneName();
        return Marshal.PtrToStringUTF8((IntPtr)ptr) ?? "";
    }
    internal static int Scene_GetEntityCount() => NativeCallbacks.Bindings.Scene_GetEntityCount();

    internal static int Scene_GetEntityCount(string sceneName)
    {
        int len = Encoding.UTF8.GetByteCount(sceneName);
        Span<byte> buf = len <= 256 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(sceneName, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_GetEntityCountByName(ptr);
    }

    internal static string Scene_GetEntityNameByUUID(ulong uuid)
    {
        byte* ptr = NativeCallbacks.Bindings.Scene_GetEntityNameByUUID(uuid);
        return Marshal.PtrToStringUTF8((IntPtr)ptr) ?? "";
    }

    internal static bool Scene_LoadAdditive(string sceneName)
    {
        int len = Encoding.UTF8.GetByteCount(sceneName);
        Span<byte> buf = len <= 256 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(sceneName, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_LoadAdditive(ptr) != 0;
    }

    internal static bool Scene_Load(string sceneName)
    {
        int len = Encoding.UTF8.GetByteCount(sceneName);
        Span<byte> buf = len <= 256 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(sceneName, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_Load(ptr) != 0;
    }

    internal static void Scene_Unload(string sceneName)
    {
        int len = Encoding.UTF8.GetByteCount(sceneName);
        Span<byte> buf = len <= 256 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(sceneName, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.Scene_Unload(ptr);
    }

    internal static bool Scene_SetActive(string sceneName)
    {
        int len = Encoding.UTF8.GetByteCount(sceneName);
        Span<byte> buf = len <= 256 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(sceneName, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_SetActive(ptr) != 0;
    }

    internal static bool Scene_Reload(string sceneName)
    {
        int len = Encoding.UTF8.GetByteCount(sceneName);
        Span<byte> buf = len <= 256 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(sceneName, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_Reload(ptr) != 0;
    }

    internal static void Scene_SetGlobalSystemEnabled(string className, bool enabled)
    {
        int len = Encoding.UTF8.GetByteCount(className);
        Span<byte> buf = len <= 512 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(className, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.Scene_SetGlobalSystemEnabled(ptr, enabled ? 1 : 0);
    }

    internal static bool Scene_SetGameSystemEnabled(string sceneName, string className, bool enabled)
    {
        static byte[] EncodeUtf8(string value)
        {
            int len = Encoding.UTF8.GetByteCount(value);
            byte[] buffer = new byte[len + 1];
            Encoding.UTF8.GetBytes(value, buffer);
            buffer[len] = 0;
            return buffer;
        }

        byte[] sceneBuffer = EncodeUtf8(sceneName);
        byte[] classBuffer = EncodeUtf8(className);
        fixed (byte* scenePtr = sceneBuffer)
        fixed (byte* classPtr = classBuffer)
        {
            return NativeCallbacks.Bindings.Scene_SetGameSystemEnabled(scenePtr, classPtr, enabled ? 1 : 0) != 0;
        }
    }

    internal static bool Scene_DoesSceneExist(string sceneName)
    {
        int len = Encoding.UTF8.GetByteCount(sceneName);
        Span<byte> buf = len <= 256 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(sceneName, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_DoesSceneExist(ptr) != 0;
    }

    internal static int Scene_GetLoadedCount() => NativeCallbacks.Bindings.Scene_GetLoadedCount();

    internal static string Scene_GetLoadedSceneNameAt(int index)
    {
        byte* ptr = NativeCallbacks.Bindings.Scene_GetLoadedSceneNameAt(index);
        return Marshal.PtrToStringUTF8((IntPtr)ptr) ?? "";
    }

    // ── Entity ──────────────────────────────────────────────────────

    internal static bool Entity_IsValid(ulong entityID) => NativeCallbacks.Bindings.Entity_IsValid(entityID) != 0;

    internal static ulong Entity_FindByName(string name)
    {
        int len = Encoding.UTF8.GetByteCount(name);
        Span<byte> buf = len <= 256 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(name, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_FindByName(ptr);
    }

    internal static void Entity_Destroy(ulong entityID) => NativeCallbacks.Bindings.Entity_Destroy(entityID);

    internal static ulong Entity_Clone(ulong sourceEntityID)
        => NativeCallbacks.Bindings.Entity_Clone(sourceEntityID);

    internal static ulong Entity_InstantiatePrefab(ulong prefabGuid)
        => NativeCallbacks.Bindings.Entity_InstantiatePrefab(prefabGuid);

    internal static int Entity_GetOrigin(ulong entityID)
        => NativeCallbacks.Bindings.Entity_GetOrigin(entityID);

    internal static ulong Entity_GetRuntimeID(ulong entityID)
        => NativeCallbacks.Bindings.Entity_GetRuntimeID(entityID);

    internal static ulong Entity_GetSceneGUID(ulong entityID)
        => NativeCallbacks.Bindings.Entity_GetSceneGUID(entityID);

    internal static ulong Entity_GetPrefabGUID(ulong entityID)
        => NativeCallbacks.Bindings.Entity_GetPrefabGUID(entityID);

    internal static bool Entity_HasComponent(ulong entityID, string componentName)
    {
        int len = Encoding.UTF8.GetByteCount(componentName);
        Span<byte> buf = len <= 256 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(componentName, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_HasComponent(entityID, ptr) != 0;
    }

    internal static bool Entity_AddComponent(ulong entityID, string componentName)
    {
        int len = Encoding.UTF8.GetByteCount(componentName);
        Span<byte> buf = len <= 256 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(componentName, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_AddComponent(entityID, ptr) != 0;
    }

    internal static bool Entity_RemoveComponent(ulong entityID, string componentName)
    {
        int len = Encoding.UTF8.GetByteCount(componentName);
        Span<byte> buf = len <= 256 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(componentName, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_RemoveComponent(entityID, ptr) != 0;
    }

    internal static string Entity_GetManagedComponentFields(ulong entityID, string componentName)
    {
        int len = Encoding.UTF8.GetByteCount(componentName);
        Span<byte> buf = len <= 256 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(componentName, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf)
        {
            byte* result = NativeCallbacks.Bindings.Entity_GetManagedComponentFields(entityID, ptr);
            return Marshal.PtrToStringUTF8((IntPtr)result) ?? "{}";
        }
    }

    internal static bool Entity_GetIsStatic(ulong entityID) => NativeCallbacks.Bindings.Entity_GetIsStatic(entityID) != 0;
    internal static void Entity_SetIsStatic(ulong entityID, bool isStatic) => NativeCallbacks.Bindings.Entity_SetIsStatic(entityID, isStatic ? 1 : 0);
    internal static bool Entity_GetIsEnabled(ulong entityID) => NativeCallbacks.Bindings.Entity_GetIsEnabled(entityID) != 0;
    internal static void Entity_SetIsEnabled(ulong entityID, bool isEnabled) => NativeCallbacks.Bindings.Entity_SetIsEnabled(entityID, isEnabled ? 1 : 0);

    internal static ulong Entity_Create(string name)
    {
        int len = Encoding.UTF8.GetByteCount(name);
        Span<byte> buf = len <= 256 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(name, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_Create(ptr);
    }

    // ── NameComponent ───────────────────────────────────────────────

    internal static string NameComponent_GetName(ulong entityID)
    {
        byte* ptr = NativeCallbacks.Bindings.NameComponent_GetName(entityID);
        return Marshal.PtrToStringUTF8((IntPtr)ptr) ?? "";
    }

    internal static void NameComponent_SetName(ulong entityID, string name)
    {
        int len = Encoding.UTF8.GetByteCount(name);
        Span<byte> buf = len <= 256 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(name, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.NameComponent_SetName(entityID, ptr);
    }

    // ── Transform2D ─────────────────────────────────────────────────

    internal static void Transform2D_GetPosition(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.Transform2D_GetPosition(id, &ox, &oy); x = ox; y = oy; }
    internal static void Transform2D_SetPosition(ulong id, float x, float y) => NativeCallbacks.Bindings.Transform2D_SetPosition(id, x, y);
    internal static float Transform2D_GetRotation(ulong id) => NativeCallbacks.Bindings.Transform2D_GetRotation(id);
    internal static void Transform2D_SetRotation(ulong id, float rotation) => NativeCallbacks.Bindings.Transform2D_SetRotation(id, rotation);
    internal static void Transform2D_GetScale(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.Transform2D_GetScale(id, &ox, &oy); x = ox; y = oy; }
    internal static void Transform2D_SetScale(ulong id, float x, float y) => NativeCallbacks.Bindings.Transform2D_SetScale(id, x, y);

    // ── SpriteRenderer ──────────────────────────────────────────────

    internal static void SpriteRenderer_GetColor(ulong id, out float r, out float g, out float b, out float a) { float cr, cg, cb, ca; NativeCallbacks.Bindings.SpriteRenderer_GetColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void SpriteRenderer_SetColor(ulong id, float r, float g, float b, float a) => NativeCallbacks.Bindings.SpriteRenderer_SetColor(id, r, g, b, a);
    internal static ulong SpriteRenderer_GetTexture(ulong id) => NativeCallbacks.Bindings.SpriteRenderer_GetTexture(id);
    internal static void SpriteRenderer_SetTexture(ulong id, ulong assetId) => NativeCallbacks.Bindings.SpriteRenderer_SetTexture(id, assetId);
    internal static int SpriteRenderer_GetSortingOrder(ulong id) => NativeCallbacks.Bindings.SpriteRenderer_GetSortingOrder(id);
    internal static void SpriteRenderer_SetSortingOrder(ulong id, int order) => NativeCallbacks.Bindings.SpriteRenderer_SetSortingOrder(id, order);
    internal static int SpriteRenderer_GetSortingLayer(ulong id) => NativeCallbacks.Bindings.SpriteRenderer_GetSortingLayer(id);
    internal static void SpriteRenderer_SetSortingLayer(ulong id, int layer) => NativeCallbacks.Bindings.SpriteRenderer_SetSortingLayer(id, layer);

    // ── Camera2D ────────────────────────────────────────────────────

    internal static float Camera2D_GetOrthographicSize(ulong id) => NativeCallbacks.Bindings.Camera2D_GetOrthographicSize(id);
    internal static void Camera2D_SetOrthographicSize(ulong id, float size) => NativeCallbacks.Bindings.Camera2D_SetOrthographicSize(id, size);
    internal static float Camera2D_GetZoom(ulong id) => NativeCallbacks.Bindings.Camera2D_GetZoom(id);
    internal static void Camera2D_SetZoom(ulong id, float zoom) => NativeCallbacks.Bindings.Camera2D_SetZoom(id, zoom);
    internal static void Camera2D_GetClearColor(ulong id, out float r, out float g, out float b, out float a) { float cr, cg, cb, ca; NativeCallbacks.Bindings.Camera2D_GetClearColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Camera2D_SetClearColor(ulong id, float r, float g, float b, float a) => NativeCallbacks.Bindings.Camera2D_SetClearColor(id, r, g, b, a);
    internal static void Camera2D_ScreenToWorld(ulong id, float sx, float sy, out float wx, out float wy) { float ox, oy; NativeCallbacks.Bindings.Camera2D_ScreenToWorld(id, sx, sy, &ox, &oy); wx = ox; wy = oy; }
    internal static float Camera2D_GetViewportWidth(ulong id) => NativeCallbacks.Bindings.Camera2D_GetViewportWidth(id);
    internal static float Camera2D_GetViewportHeight(ulong id) => NativeCallbacks.Bindings.Camera2D_GetViewportHeight(id);
    internal static ulong Camera2D_GetMainEntity() => NativeCallbacks.Bindings.Camera2D_GetMainEntity();

    // ── Rigidbody2D ─────────────────────────────────────────────────

    internal static void Rigidbody2D_ApplyForce(ulong id, float fx, float fy, bool wake) => NativeCallbacks.Bindings.Rigidbody2D_ApplyForce(id, fx, fy, wake ? 1 : 0);
    internal static void Rigidbody2D_ApplyImpulse(ulong id, float ix, float iy, bool wake) => NativeCallbacks.Bindings.Rigidbody2D_ApplyImpulse(id, ix, iy, wake ? 1 : 0);
    internal static void Rigidbody2D_GetLinearVelocity(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.Rigidbody2D_GetLinearVelocity(id, &ox, &oy); x = ox; y = oy; }
    internal static void Rigidbody2D_SetLinearVelocity(ulong id, float x, float y) => NativeCallbacks.Bindings.Rigidbody2D_SetLinearVelocity(id, x, y);
    internal static float Rigidbody2D_GetAngularVelocity(ulong id) => NativeCallbacks.Bindings.Rigidbody2D_GetAngularVelocity(id);
    internal static void Rigidbody2D_SetAngularVelocity(ulong id, float v) => NativeCallbacks.Bindings.Rigidbody2D_SetAngularVelocity(id, v);
    internal static int Rigidbody2D_GetBodyType(ulong id) => NativeCallbacks.Bindings.Rigidbody2D_GetBodyType(id);
    internal static void Rigidbody2D_SetBodyType(ulong id, int type) => NativeCallbacks.Bindings.Rigidbody2D_SetBodyType(id, type);
    internal static float Rigidbody2D_GetGravityScale(ulong id) => NativeCallbacks.Bindings.Rigidbody2D_GetGravityScale(id);
    internal static void Rigidbody2D_SetGravityScale(ulong id, float scale) => NativeCallbacks.Bindings.Rigidbody2D_SetGravityScale(id, scale);
    internal static float Rigidbody2D_GetMass(ulong id) => NativeCallbacks.Bindings.Rigidbody2D_GetMass(id);
    internal static void Rigidbody2D_SetMass(ulong id, float mass) => NativeCallbacks.Bindings.Rigidbody2D_SetMass(id, mass);

    // ── BoxCollider2D ───────────────────────────────────────────────

    internal static void BoxCollider2D_GetScale(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.BoxCollider2D_GetScale(id, &ox, &oy); x = ox; y = oy; }
    internal static void BoxCollider2D_GetCenter(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.BoxCollider2D_GetCenter(id, &ox, &oy); x = ox; y = oy; }
    internal static void BoxCollider2D_SetEnabled(ulong id, bool enabled) => NativeCallbacks.Bindings.BoxCollider2D_SetEnabled(id, enabled ? 1 : 0);

    // ── AudioSource ─────────────────────────────────────────────────

    internal static void AudioSource_Play(ulong id) => NativeCallbacks.Bindings.AudioSource_Play(id);
    internal static void AudioSource_Pause(ulong id) => NativeCallbacks.Bindings.AudioSource_Pause(id);
    internal static void AudioSource_Stop(ulong id) => NativeCallbacks.Bindings.AudioSource_Stop(id);
    internal static void AudioSource_Resume(ulong id) => NativeCallbacks.Bindings.AudioSource_Resume(id);
    internal static float AudioSource_GetVolume(ulong id) => NativeCallbacks.Bindings.AudioSource_GetVolume(id);
    internal static void AudioSource_SetVolume(ulong id, float v) => NativeCallbacks.Bindings.AudioSource_SetVolume(id, v);
    internal static float AudioSource_GetPitch(ulong id) => NativeCallbacks.Bindings.AudioSource_GetPitch(id);
    internal static void AudioSource_SetPitch(ulong id, float p) => NativeCallbacks.Bindings.AudioSource_SetPitch(id, p);
    internal static bool AudioSource_GetLoop(ulong id) => NativeCallbacks.Bindings.AudioSource_GetLoop(id) != 0;
    internal static void AudioSource_SetLoop(ulong id, bool loop) => NativeCallbacks.Bindings.AudioSource_SetLoop(id, loop ? 1 : 0);
    internal static bool AudioSource_IsPlaying(ulong id) => NativeCallbacks.Bindings.AudioSource_IsPlaying(id) != 0;
    internal static bool AudioSource_IsPaused(ulong id) => NativeCallbacks.Bindings.AudioSource_IsPaused(id) != 0;
    internal static ulong AudioSource_GetAudio(ulong id) => NativeCallbacks.Bindings.AudioSource_GetAudio(id);
    internal static void AudioSource_SetAudio(ulong id, ulong assetId) => NativeCallbacks.Bindings.AudioSource_SetAudio(id, assetId);

    // ── Axiom-Physics ────────────────────────────────────────────────

    internal static int FastBody2D_GetBodyType(ulong id) => NativeCallbacks.Bindings.FastBody2D_GetBodyType(id);
    internal static void FastBody2D_SetBodyType(ulong id, int type) => NativeCallbacks.Bindings.FastBody2D_SetBodyType(id, type);
    internal static float FastBody2D_GetMass(ulong id) => NativeCallbacks.Bindings.FastBody2D_GetMass(id);
    internal static void FastBody2D_SetMass(ulong id, float mass) => NativeCallbacks.Bindings.FastBody2D_SetMass(id, mass);
    internal static bool FastBody2D_GetUseGravity(ulong id) => NativeCallbacks.Bindings.FastBody2D_GetUseGravity(id) != 0;
    internal static void FastBody2D_SetUseGravity(ulong id, bool enabled) => NativeCallbacks.Bindings.FastBody2D_SetUseGravity(id, enabled ? 1 : 0);
    internal static void FastBody2D_GetVelocity(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.FastBody2D_GetVelocity(id, &ox, &oy); x = ox; y = oy; }
    internal static void FastBody2D_SetVelocity(ulong id, float x, float y) => NativeCallbacks.Bindings.FastBody2D_SetVelocity(id, x, y);
    internal static void FastBoxCollider2D_GetHalfExtents(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.FastBoxCollider2D_GetHalfExtents(id, &ox, &oy); x = ox; y = oy; }
    internal static void FastBoxCollider2D_SetHalfExtents(ulong id, float x, float y) => NativeCallbacks.Bindings.FastBoxCollider2D_SetHalfExtents(id, x, y);
    internal static float FastCircleCollider2D_GetRadius(ulong id) => NativeCallbacks.Bindings.FastCircleCollider2D_GetRadius(id);
    internal static void FastCircleCollider2D_SetRadius(ulong id, float radius) => NativeCallbacks.Bindings.FastCircleCollider2D_SetRadius(id, radius);

    // ── Scene Query ─────────────────────────────────────────────

    internal static int Scene_QueryEntities(string componentNames, Span<ulong> outEntityIDs)
    {
        int len = Encoding.UTF8.GetByteCount(componentNames);
        Span<byte> buf = len <= 512 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(componentNames, buf);
        buf[len] = 0;
        fixed (byte* namePtr = buf)
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Scene_QueryEntities(namePtr, idPtr, outEntityIDs.Length);
        }
    }

    internal static int Scene_QueryEntitiesFiltered(
        string withComponents, string withoutComponents, string mustHaveComponents,
        int enableFilter, Span<ulong> outEntityIDs)
    {
        static byte[] EncodeUtf8(string s)
        {
            if (string.IsNullOrEmpty(s)) return new byte[] { 0 };
            int len = Encoding.UTF8.GetByteCount(s);
            byte[] buf = new byte[len + 1];
            Encoding.UTF8.GetBytes(s, buf);
            buf[len] = 0;
            return buf;
        }

        byte[] withBuf = EncodeUtf8(withComponents);
        byte[] withoutBuf = EncodeUtf8(withoutComponents);
        byte[] mustHaveBuf = EncodeUtf8(mustHaveComponents);

        fixed (byte* withPtr = withBuf)
        fixed (byte* withoutPtr = withoutBuf)
        fixed (byte* mustHavePtr = mustHaveBuf)
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Scene_QueryEntitiesFiltered(
                withPtr, withoutPtr, mustHavePtr, enableFilter, idPtr, outEntityIDs.Length);
        }
    }

    internal static int Scene_QueryEntities(string sceneName, string componentNames, Span<ulong> outEntityIDs)
    {
        int sceneLen = Encoding.UTF8.GetByteCount(sceneName);
        int namesLen = Encoding.UTF8.GetByteCount(componentNames);
        Span<byte> sceneBuf = sceneLen <= 256 ? stackalloc byte[sceneLen + 1] : new byte[sceneLen + 1];
        Span<byte> namesBuf = namesLen <= 512 ? stackalloc byte[namesLen + 1] : new byte[namesLen + 1];
        Encoding.UTF8.GetBytes(sceneName, sceneBuf);
        Encoding.UTF8.GetBytes(componentNames, namesBuf);
        sceneBuf[sceneLen] = 0;
        namesBuf[namesLen] = 0;
        fixed (byte* scenePtr = sceneBuf)
        fixed (byte* namesPtr = namesBuf)
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Scene_QueryEntitiesInScene(
                scenePtr, namesPtr, idPtr, outEntityIDs.Length);
        }
    }

    internal static int Scene_QueryEntitiesFiltered(
        string sceneName, string withComponents, string withoutComponents,
        string mustHaveComponents, int enableFilter, Span<ulong> outEntityIDs)
    {
        static byte[] EncodeUtf8(string s)
        {
            if (string.IsNullOrEmpty(s)) return new byte[] { 0 };
            int len = Encoding.UTF8.GetByteCount(s);
            byte[] buf = new byte[len + 1];
            Encoding.UTF8.GetBytes(s, buf);
            buf[len] = 0;
            return buf;
        }

        byte[] sceneBuf = EncodeUtf8(sceneName);
        byte[] withBuf = EncodeUtf8(withComponents);
        byte[] withoutBuf = EncodeUtf8(withoutComponents);
        byte[] mustHaveBuf = EncodeUtf8(mustHaveComponents);

        fixed (byte* scenePtr = sceneBuf)
        fixed (byte* withPtr = withBuf)
        fixed (byte* withoutPtr = withoutBuf)
        fixed (byte* mustHavePtr = mustHaveBuf)
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Scene_QueryEntitiesFilteredInScene(
                scenePtr, withPtr, withoutPtr, mustHavePtr, enableFilter, idPtr, outEntityIDs.Length);
        }
    }

    // ── ParticleSystem2D ────────────────────────────────────────

    internal static bool Asset_IsValid(ulong assetId) => assetId != 0 && NativeCallbacks.Bindings.Asset_IsValid(assetId) != 0;

    internal static ulong Asset_GetOrCreateUUIDFromPath(string path)
    {
        if (string.IsNullOrEmpty(path))
            return 0;

        int len = Encoding.UTF8.GetByteCount(path);
        Span<byte> buf = len <= 512 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(path, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Asset_GetOrCreateUUIDFromPath(ptr);
    }

    internal static string Asset_GetPath(ulong assetId)
    {
        byte* ptr = NativeCallbacks.Bindings.Asset_GetPath(assetId);
        return Marshal.PtrToStringUTF8((IntPtr)ptr) ?? "";
    }

    internal static string Asset_GetDisplayName(ulong assetId)
    {
        byte* ptr = NativeCallbacks.Bindings.Asset_GetDisplayName(assetId);
        return Marshal.PtrToStringUTF8((IntPtr)ptr) ?? "";
    }

    internal static int Asset_GetKind(ulong assetId) => NativeCallbacks.Bindings.Asset_GetKind(assetId);

    internal static string Asset_FindAll(string pathPrefix, int kind)
    {
        pathPrefix ??= "";
        int len = Encoding.UTF8.GetByteCount(pathPrefix);
        Span<byte> buf = len <= 512 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(pathPrefix, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf)
        {
            byte* result = NativeCallbacks.Bindings.Asset_FindAll(ptr, kind);
            return Marshal.PtrToStringUTF8((IntPtr)result) ?? "[]";
        }
    }

    internal static bool Texture_LoadAsset(ulong assetId) => assetId != 0 && NativeCallbacks.Bindings.Texture_LoadAsset(assetId) != 0;
    internal static int Texture_GetWidth(ulong assetId) => NativeCallbacks.Bindings.Texture_GetWidth(assetId);
    internal static int Texture_GetHeight(ulong assetId) => NativeCallbacks.Bindings.Texture_GetHeight(assetId);

    internal static bool Audio_LoadAsset(ulong assetId) => assetId != 0 && NativeCallbacks.Bindings.Audio_LoadAsset(assetId) != 0;
    internal static void Audio_PlayOneShotAsset(ulong assetId, float volume) => NativeCallbacks.Bindings.Audio_PlayOneShotAsset(assetId, volume);

    internal static void ParticleSystem2D_Play(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_Play(id);
    internal static void ParticleSystem2D_Pause(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_Pause(id);
    internal static void ParticleSystem2D_Stop(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_Stop(id);
    internal static bool ParticleSystem2D_IsPlaying(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_IsPlaying(id) != 0;
    internal static bool ParticleSystem2D_GetPlayOnAwake(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_GetPlayOnAwake(id) != 0;
    internal static void ParticleSystem2D_SetPlayOnAwake(ulong id, bool enabled) => NativeCallbacks.Bindings.ParticleSystem2D_SetPlayOnAwake(id, enabled ? 1 : 0);
    internal static void ParticleSystem2D_GetColor(ulong id, out float r, out float g, out float b, out float a) { float cr, cg, cb, ca; NativeCallbacks.Bindings.ParticleSystem2D_GetColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void ParticleSystem2D_SetColor(ulong id, float r, float g, float b, float a) => NativeCallbacks.Bindings.ParticleSystem2D_SetColor(id, r, g, b, a);
    internal static float ParticleSystem2D_GetLifeTime(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_GetLifeTime(id);
    internal static void ParticleSystem2D_SetLifeTime(ulong id, float v) => NativeCallbacks.Bindings.ParticleSystem2D_SetLifeTime(id, v);
    internal static float ParticleSystem2D_GetSpeed(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_GetSpeed(id);
    internal static void ParticleSystem2D_SetSpeed(ulong id, float v) => NativeCallbacks.Bindings.ParticleSystem2D_SetSpeed(id, v);
    internal static float ParticleSystem2D_GetScale(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_GetScale(id);
    internal static void ParticleSystem2D_SetScale(ulong id, float v) => NativeCallbacks.Bindings.ParticleSystem2D_SetScale(id, v);
    internal static int ParticleSystem2D_GetEmitOverTime(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_GetEmitOverTime(id);
    internal static void ParticleSystem2D_SetEmitOverTime(ulong id, int v) => NativeCallbacks.Bindings.ParticleSystem2D_SetEmitOverTime(id, v);
    internal static void ParticleSystem2D_Emit(ulong id, int count) => NativeCallbacks.Bindings.ParticleSystem2D_Emit(id, count);

    // ── Gizmos ──────────────────────────────────────────────────────

    internal static void Gizmo_DrawLine(float x1, float y1, float x2, float y2) => NativeCallbacks.Bindings.Gizmo_DrawLine(x1, y1, x2, y2);
    internal static void Gizmo_DrawSquare(float cx, float cy, float sx, float sy, float deg) => NativeCallbacks.Bindings.Gizmo_DrawSquare(cx, cy, sx, sy, deg);
    internal static void Gizmo_DrawCircle(float cx, float cy, float r, int seg) => NativeCallbacks.Bindings.Gizmo_DrawCircle(cx, cy, r, seg);
    internal static void Gizmo_SetColor(float r, float g, float b, float a) => NativeCallbacks.Bindings.Gizmo_SetColor(r, g, b, a);
    internal static void Gizmo_GetColor(out float r, out float g, out float b, out float a) { float cr, cg, cb, ca; NativeCallbacks.Bindings.Gizmo_GetColor(&cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static float Gizmo_GetLineWidth() => NativeCallbacks.Bindings.Gizmo_GetLineWidth();
    internal static void Gizmo_SetLineWidth(float w) => NativeCallbacks.Bindings.Gizmo_SetLineWidth(w);

    // ── Physics2D ───────────────────────────────────────────────────

    internal static bool Physics2D_Raycast(float originX, float originY, float dirX, float dirY, float distance,
        out ulong hitEntityID, out float hitX, out float hitY, out float hitNormalX, out float hitNormalY, out float hitDistance)
    {
        ulong eid; float hx, hy, hnx, hny, hd;
        int result = NativeCallbacks.Bindings.Physics2D_Raycast(originX, originY, dirX, dirY, distance, &eid, &hx, &hy, &hnx, &hny, &hd);
        hitEntityID = eid; hitX = hx; hitY = hy; hitNormalX = hnx; hitNormalY = hny; hitDistance = hd;
        return result != 0;
    }

    internal static bool Physics2D_OverlapCircle(float originX, float originY, float radius, int mode, out ulong entityID)
    {
        ulong id;
        int result = NativeCallbacks.Bindings.Physics2D_OverlapCircle(originX, originY, radius, mode, &id);
        entityID = id;
        return result != 0 && id != 0;
    }

    internal static bool Physics2D_OverlapBox(float originX, float originY, float halfX, float halfY, float degrees, int mode, out ulong entityID)
    {
        ulong id;
        int result = NativeCallbacks.Bindings.Physics2D_OverlapBox(originX, originY, halfX, halfY, degrees, mode, &id);
        entityID = id;
        return result != 0 && id != 0;
    }

    internal static bool Physics2D_OverlapPolygon(float originX, float originY, float[] points, int mode, out ulong entityID)
    {
        ulong id;
        fixed (float* pointsPtr = points)
        {
            int result = NativeCallbacks.Bindings.Physics2D_OverlapPolygon(originX, originY, pointsPtr, points.Length / 2, mode, &id);
            entityID = id;
            return result != 0 && id != 0;
        }
    }

    internal static int Physics2D_OverlapCircleAll(float originX, float originY, float radius, Span<ulong> outEntityIDs)
    {
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Physics2D_OverlapCircleAll(originX, originY, radius, idPtr, outEntityIDs.Length);
        }
    }

    internal static int Physics2D_OverlapBoxAll(float originX, float originY, float halfX, float halfY, float degrees, Span<ulong> outEntityIDs)
    {
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Physics2D_OverlapBoxAll(originX, originY, halfX, halfY, degrees, idPtr, outEntityIDs.Length);
        }
    }

    internal static int Physics2D_OverlapPolygonAll(float originX, float originY, float[] points, Span<ulong> outEntityIDs)
    {
        fixed (float* pointsPtr = points)
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Physics2D_OverlapPolygonAll(originX, originY, pointsPtr, points.Length / 2, idPtr, outEntityIDs.Length);
        }
    }

    internal static bool Physics2D_ContainsPoint(float originX, float originY, int mode, out ulong entityID)
    {
        ulong id;
        int result = NativeCallbacks.Bindings.Physics2D_ContainsPoint(originX, originY, mode, &id);
        entityID = id;
        return result != 0 && id != 0;
    }

    internal static int Physics2D_ContainsPointAll(float originX, float originY, Span<ulong> outEntityIDs)
    {
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Physics2D_ContainsPointAll(originX, originY, idPtr, outEntityIDs.Length);
        }
    }
}

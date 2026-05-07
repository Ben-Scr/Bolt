using System;
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

    private static byte[] EncodeUtf8Z(string? value)
    {
        value ??= "";
        int len = Encoding.UTF8.GetByteCount(value);
        byte[] buffer = new byte[len + 1];
        Encoding.UTF8.GetBytes(value, buffer);
        buffer[len] = 0;
        return buffer;
    }

    private static string DecodeNativeString(byte[] buffer, int byteCount)
    {
        byteCount = Math.Clamp(byteCount, 0, Math.Max(0, buffer.Length - 1));
        return byteCount == 0 ? "" : Encoding.UTF8.GetString(buffer, 0, byteCount);
    }

    private static string ReadNativeString(delegate* unmanaged<byte*, int, int> fn)
    {
        int required = fn(null, 0);
        if (required <= 0) return "";

        byte[] buffer = new byte[required + 1];
        fixed (byte* ptr = buffer)
        {
            int written = fn(ptr, buffer.Length);
            return DecodeNativeString(buffer, written);
        }
    }

    private static string ReadNativeString(delegate* unmanaged<ulong, byte*, int, int> fn, ulong value)
    {
        int required = fn(value, null, 0);
        if (required <= 0) return "";

        byte[] buffer = new byte[required + 1];
        fixed (byte* ptr = buffer)
        {
            int written = fn(value, ptr, buffer.Length);
            return DecodeNativeString(buffer, written);
        }
    }

    private static string ReadNativeString(delegate* unmanaged<int, byte*, int, int> fn, int value)
    {
        int required = fn(value, null, 0);
        if (required <= 0) return "";

        byte[] buffer = new byte[required + 1];
        fixed (byte* ptr = buffer)
        {
            int written = fn(value, ptr, buffer.Length);
            return DecodeNativeString(buffer, written);
        }
    }

    private static void CallStringBinding(delegate* unmanaged<byte*, void> fn, string? message)
    {
        message ??= "";
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
        return ReadNativeString(NativeCallbacks.Bindings.Scene_GetActiveSceneNameBuffer);
    }
    internal static int Scene_GetEntityCount() => NativeCallbacks.Bindings.Scene_GetEntityCount();

    internal static int Scene_GetEntityCount(string sceneName)
    {
        byte[] buf = EncodeUtf8Z(sceneName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_GetEntityCountByName(ptr);
    }

    internal static string Scene_GetEntityNameByUUID(ulong uuid)
    {
        return ReadNativeString(NativeCallbacks.Bindings.Scene_GetEntityNameByUUIDBuffer, uuid);
    }

    internal static bool Scene_LoadAdditive(string sceneName)
    {
        byte[] buf = EncodeUtf8Z(sceneName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_LoadAdditive(ptr) != 0;
    }

    internal static bool Scene_Load(string sceneName)
    {
        byte[] buf = EncodeUtf8Z(sceneName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_Load(ptr) != 0;
    }

    internal static void Scene_Unload(string sceneName)
    {
        byte[] buf = EncodeUtf8Z(sceneName);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.Scene_Unload(ptr);
    }

    internal static bool Scene_SetActive(string sceneName)
    {
        byte[] buf = EncodeUtf8Z(sceneName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_SetActive(ptr) != 0;
    }

    internal static bool Scene_Reload(string sceneName)
    {
        byte[] buf = EncodeUtf8Z(sceneName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_Reload(ptr) != 0;
    }

    internal static void Scene_SetGlobalSystemEnabled(string className, bool enabled)
    {
        byte[] buf = EncodeUtf8Z(className);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.Scene_SetGlobalSystemEnabled(ptr, enabled ? 1 : 0);
    }

    internal static bool Scene_SetGameSystemEnabled(string sceneName, string className, bool enabled)
    {
        byte[] sceneBuffer = EncodeUtf8Z(sceneName);
        byte[] classBuffer = EncodeUtf8Z(className);
        fixed (byte* scenePtr = sceneBuffer)
        fixed (byte* classPtr = classBuffer)
        {
            return NativeCallbacks.Bindings.Scene_SetGameSystemEnabled(scenePtr, classPtr, enabled ? 1 : 0) != 0;
        }
    }

    internal static bool Scene_DoesSceneExist(string sceneName)
    {
        byte[] buf = EncodeUtf8Z(sceneName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_DoesSceneExist(ptr) != 0;
    }

    internal static int Scene_GetLoadedCount() => NativeCallbacks.Bindings.Scene_GetLoadedCount();

    internal static string Scene_GetLoadedSceneNameAt(int index)
    {
        return ReadNativeString(NativeCallbacks.Bindings.Scene_GetLoadedSceneNameAtBuffer, index);
    }

    // ── Entity ──────────────────────────────────────────────────────

    internal static bool Entity_IsValid(ulong entityID) => NativeCallbacks.Bindings.Entity_IsValid(entityID) != 0;

    internal static ulong Entity_FindByName(string name)
    {
        byte[] buf = EncodeUtf8Z(name);
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
        byte[] buf = EncodeUtf8Z(componentName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_HasComponent(entityID, ptr) != 0;
    }

    internal static bool Entity_AddComponent(ulong entityID, string componentName)
    {
        byte[] buf = EncodeUtf8Z(componentName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_AddComponent(entityID, ptr) != 0;
    }

    internal static bool Entity_RemoveComponent(ulong entityID, string componentName)
    {
        byte[] buf = EncodeUtf8Z(componentName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_RemoveComponent(entityID, ptr) != 0;
    }

    internal static string Entity_GetManagedComponentFields(ulong entityID, string componentName)
    {
        byte[] buf = EncodeUtf8Z(componentName);
        fixed (byte* ptr = buf)
        {
            int required = NativeCallbacks.Bindings.Entity_GetManagedComponentFieldsBuffer(entityID, ptr, null, 0);
            if (required <= 0) return "{}";

            byte[] output = new byte[required + 1];
            fixed (byte* outPtr = output)
            {
                int written = NativeCallbacks.Bindings.Entity_GetManagedComponentFieldsBuffer(entityID, ptr, outPtr, output.Length);
                return DecodeNativeString(output, written);
            }
        }
    }

    internal static bool Entity_GetIsStatic(ulong entityID) => NativeCallbacks.Bindings.Entity_GetIsStatic(entityID) != 0;
    internal static void Entity_SetIsStatic(ulong entityID, bool isStatic) => NativeCallbacks.Bindings.Entity_SetIsStatic(entityID, isStatic ? 1 : 0);
    internal static bool Entity_GetIsEnabled(ulong entityID) => NativeCallbacks.Bindings.Entity_GetIsEnabled(entityID) != 0;
    internal static void Entity_SetIsEnabled(ulong entityID, bool isEnabled) => NativeCallbacks.Bindings.Entity_SetIsEnabled(entityID, isEnabled ? 1 : 0);

    internal static ulong Entity_Create(string name)
    {
        byte[] buf = EncodeUtf8Z(name);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_Create(ptr);
    }

    // ── NameComponent ───────────────────────────────────────────────

    internal static string NameComponent_GetName(ulong entityID)
    {
        return ReadNativeString(NativeCallbacks.Bindings.NameComponent_GetNameBuffer, entityID);
    }

    internal static void NameComponent_SetName(ulong entityID, string name)
    {
        byte[] buf = EncodeUtf8Z(name);
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

    // ── TextRenderer ────────────────────────────────────────────────

    internal static string TextRenderer_GetText(ulong entityID)
    {
        return ReadNativeString(NativeCallbacks.Bindings.TextRenderer_GetTextBuffer, entityID);
    }

    internal static void TextRenderer_SetText(ulong entityID, string text)
    {
        byte[] buf = EncodeUtf8Z(text);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.TextRenderer_SetText(entityID, ptr);
    }

    internal static ulong TextRenderer_GetFont(ulong id) => NativeCallbacks.Bindings.TextRenderer_GetFont(id);
    internal static void TextRenderer_SetFont(ulong id, ulong assetId) => NativeCallbacks.Bindings.TextRenderer_SetFont(id, assetId);
    internal static float TextRenderer_GetFontSize(ulong id) => NativeCallbacks.Bindings.TextRenderer_GetFontSize(id);
    internal static void TextRenderer_SetFontSize(ulong id, float size) => NativeCallbacks.Bindings.TextRenderer_SetFontSize(id, size);
    internal static void TextRenderer_GetColor(ulong id, out float r, out float g, out float b, out float a) { float cr, cg, cb, ca; NativeCallbacks.Bindings.TextRenderer_GetColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void TextRenderer_SetColor(ulong id, float r, float g, float b, float a) => NativeCallbacks.Bindings.TextRenderer_SetColor(id, r, g, b, a);
    internal static float TextRenderer_GetLetterSpacing(ulong id) => NativeCallbacks.Bindings.TextRenderer_GetLetterSpacing(id);
    internal static void TextRenderer_SetLetterSpacing(ulong id, float spacing) => NativeCallbacks.Bindings.TextRenderer_SetLetterSpacing(id, spacing);
    internal static int TextRenderer_GetHAlign(ulong id) => NativeCallbacks.Bindings.TextRenderer_GetHAlign(id);
    internal static void TextRenderer_SetHAlign(ulong id, int alignment) => NativeCallbacks.Bindings.TextRenderer_SetHAlign(id, alignment);
    internal static int TextRenderer_GetSortingOrder(ulong id) => NativeCallbacks.Bindings.TextRenderer_GetSortingOrder(id);
    internal static void TextRenderer_SetSortingOrder(ulong id, int order) => NativeCallbacks.Bindings.TextRenderer_SetSortingOrder(id, order);
    internal static int TextRenderer_GetSortingLayer(ulong id) => NativeCallbacks.Bindings.TextRenderer_GetSortingLayer(id);
    internal static void TextRenderer_SetSortingLayer(ulong id, int layer) => NativeCallbacks.Bindings.TextRenderer_SetSortingLayer(id, layer);

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
        byte[] buf = EncodeUtf8Z(componentNames);
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
        byte[] withBuf = EncodeUtf8Z(withComponents);
        byte[] withoutBuf = EncodeUtf8Z(withoutComponents);
        byte[] mustHaveBuf = EncodeUtf8Z(mustHaveComponents);

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
        byte[] sceneBuf = EncodeUtf8Z(sceneName);
        byte[] namesBuf = EncodeUtf8Z(componentNames);
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
        byte[] sceneBuf = EncodeUtf8Z(sceneName);
        byte[] withBuf = EncodeUtf8Z(withComponents);
        byte[] withoutBuf = EncodeUtf8Z(withoutComponents);
        byte[] mustHaveBuf = EncodeUtf8Z(mustHaveComponents);

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

        byte[] buf = EncodeUtf8Z(path);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Asset_GetOrCreateUUIDFromPath(ptr);
    }

    internal static string Asset_GetPath(ulong assetId)
    {
        return ReadNativeString(NativeCallbacks.Bindings.Asset_GetPathBuffer, assetId);
    }

    internal static string Asset_GetDisplayName(ulong assetId)
    {
        return ReadNativeString(NativeCallbacks.Bindings.Asset_GetDisplayNameBuffer, assetId);
    }

    internal static int Asset_GetKind(ulong assetId) => NativeCallbacks.Bindings.Asset_GetKind(assetId);

    internal static string Asset_FindAll(string pathPrefix, int kind)
    {
        byte[] buf = EncodeUtf8Z(pathPrefix);
        fixed (byte* ptr = buf)
        {
            int required = NativeCallbacks.Bindings.Asset_FindAllBuffer(ptr, kind, null, 0);
            if (required <= 0) return "[]";

            byte[] output = new byte[required + 1];
            fixed (byte* outPtr = output)
            {
                int written = NativeCallbacks.Bindings.Asset_FindAllBuffer(ptr, kind, outPtr, output.Length);
                return DecodeNativeString(output, written);
            }
        }
    }

    internal static bool Texture_LoadAsset(ulong assetId) => assetId != 0 && NativeCallbacks.Bindings.Texture_LoadAsset(assetId) != 0;
    internal static int Texture_GetWidth(ulong assetId) => NativeCallbacks.Bindings.Texture_GetWidth(assetId);
    internal static int Texture_GetHeight(ulong assetId) => NativeCallbacks.Bindings.Texture_GetHeight(assetId);

    internal static bool Audio_LoadAsset(ulong assetId) => assetId != 0 && NativeCallbacks.Bindings.Audio_LoadAsset(assetId) != 0;
    internal static void Audio_PlayOneShotAsset(ulong assetId, float volume) => NativeCallbacks.Bindings.Audio_PlayOneShotAsset(assetId, volume);

    internal static bool Font_LoadAsset(ulong assetId) => assetId != 0 && NativeCallbacks.Bindings.Font_LoadAsset(assetId) != 0;

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

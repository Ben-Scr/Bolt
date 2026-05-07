using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Text.Json;
using Axiom.Interop;

namespace Axiom;

public enum EntityOrigin
{
    Scene = 0,
    Prefab = 1,
    Runtime = 2
}

public class Entity : IEquatable<Entity>
{
    public static readonly Entity Invalid = new(0);

    private static readonly Dictionary<(ulong EntityID, Type ComponentType), Component> s_ManagedComponentStore = new();

    private readonly Dictionary<Type, Component> m_ComponentCache = new();
    private readonly ulong m_PrefabAssetGUID;
    private Transform2D? m_TransformComponent;

    protected Entity()
    {
        ID = 0;
        m_PrefabAssetGUID = 0;
    }

    internal Entity(ulong id)
    {
        ID = id;
        m_PrefabAssetGUID = 0;
    }

    private Entity(ulong id, ulong prefabAssetGuid)
    {
        ID = id;
        m_PrefabAssetGUID = prefabAssetGuid;
    }

    public readonly ulong ID;

    internal bool IsPrefabAsset => m_PrefabAssetGUID != 0;
    public ulong RuntimeID => IsPrefabAsset ? 0 : InternalCalls.Entity_GetRuntimeID(ID);
    public ulong SceneGUID => IsPrefabAsset ? 0 : InternalCalls.Entity_GetSceneGUID(ID);
    public ulong PrefabGUID => IsPrefabAsset ? m_PrefabAssetGUID : InternalCalls.Entity_GetPrefabGUID(ID);
    public EntityOrigin Origin => IsPrefabAsset ? EntityOrigin.Prefab : (EntityOrigin)InternalCalls.Entity_GetOrigin(ID);
    public bool IsSceneEntity => !IsPrefabAsset && Origin == EntityOrigin.Scene;
    public bool IsPrefabInstance => IsPrefabAsset || (!IsPrefabAsset && Origin == EntityOrigin.Prefab);
    public bool IsRuntime => !IsPrefabAsset && Origin == EntityOrigin.Runtime;
    public bool IsStatic
    {
        get => !IsPrefabAsset && InternalCalls.Entity_GetIsStatic(ID);
        set
        {
            if (!IsPrefabAsset)
                InternalCalls.Entity_SetIsStatic(ID, value);
        }
    }

    public bool IsEnabled
    {
        get => !IsPrefabAsset && InternalCalls.Entity_GetIsEnabled(ID);
        set
        {
            if (!IsPrefabAsset)
                InternalCalls.Entity_SetIsEnabled(ID, value);
        }
    }

    public Transform2D Transform
    {
        get
        {
            if (m_TransformComponent == null)
            {
                m_TransformComponent = GetComponent<Transform2D>();
                if (m_TransformComponent == null)
                    throw new InvalidOperationException("Entity does not have a Transform2D component");
            }
            return m_TransformComponent;
        }
        set => m_TransformComponent = value;
    }

    public string Name
    {
        get
        {
            if (IsPrefabAsset)
                return InternalCalls.Asset_GetDisplayName(m_PrefabAssetGUID);

            return GetComponent<NameComponent>()?.Name ?? "";
        }
        set
        {
            if (IsPrefabAsset)
                return;

            NameComponent? nameComp = GetComponent<NameComponent>() ?? AddComponent<NameComponent>();
            if (nameComp != null)
                nameComp.Name = value ?? "";
        }
    }

    private static readonly Dictionary<Type, string> s_NativeComponentNames = new()
    {
        { typeof(NameComponent),         "Name" },
        { typeof(Transform2D),           "Transform 2D" },
        { typeof(SpriteRenderer),        "Sprite Renderer" },
        { typeof(TextRenderer),          "Text Renderer" },
        { typeof(Camera2D),              "Camera 2D" },
        { typeof(Rigidbody2D),           "Rigidbody 2D" },
        { typeof(BoxCollider2D),         "Box Collider 2D" },
        { typeof(AudioSource),           "Audio Source" },
        { typeof(FastBody2D),            "Fast Body 2D" },
        { typeof(FastBoxCollider2D),     "Fast Box Collider 2D" },
        { typeof(FastCircleCollider2D),  "Fast Circle Collider 2D" },
        { typeof(ParticleSystem2D),      "Particle System 2D" },
    };

    private static string? GetNativeName<T>() where T : Component, new() => GetComponentName(typeof(T));

    private static string? GetComponentName(Type type)
    {
        if (s_NativeComponentNames.TryGetValue(type, out string? name))
            return name;

        return type.IsSubclassOf(typeof(Component)) ? type.Name : null;
    }

    internal static Entity FromPrefabGUID(ulong prefabGuid)
        => prefabGuid != 0 ? new Entity(0, prefabGuid) : Invalid;

    internal static string? GetNativeComponentName<T>() where T : Component, new() => GetNativeName<T>();
    internal static bool TryGetNativeComponentName(Type type, out string? name) => s_NativeComponentNames.TryGetValue(type, out name);

    private void InvalidateCachedComponent(Type type)
    {
        if (m_ComponentCache.Remove(type, out Component? cached))
            cached.Invalidate();

        if (type == typeof(Transform2D))
            m_TransformComponent = null;

        if (!s_NativeComponentNames.ContainsKey(type))
            InvalidateManagedComponent(ID, type);
    }

    private void InvalidateAllCachedComponents()
    {
        foreach (Component component in m_ComponentCache.Values)
            component.Invalidate();

        m_ComponentCache.Clear();
        m_TransformComponent = null;
        InvalidateManagedComponents(ID);
    }

    private static void InvalidateManagedComponent(ulong entityID, Type type)
    {
        if (s_ManagedComponentStore.Remove((entityID, type), out Component? component))
            component.Invalidate();
    }

    private static void InvalidateManagedComponents(ulong entityID)
    {
        List<(ulong EntityID, Type ComponentType)> keysToRemove = new();
        foreach (var key in s_ManagedComponentStore.Keys)
        {
            if (key.EntityID == entityID)
                keysToRemove.Add(key);
        }

        foreach (var key in keysToRemove)
        {
            if (s_ManagedComponentStore.Remove(key, out Component? component))
                component.Invalidate();
        }
    }

    // Hot-reload support: the AssemblyLoadContext that owns user-defined Component
    // subclasses can only unload once *every* instance is unreachable. Component
    // entries in s_ManagedComponentStore reference user types and therefore root
    // the context — clearing this dictionary on assembly unload is required for
    // the ALC to actually collect and free its types.
    internal static void ClearManagedComponentStore()
    {
        foreach (var component in s_ManagedComponentStore.Values)
            component.Invalidate();
        s_ManagedComponentStore.Clear();
    }

    public T? AddComponent<T>() where T : Component, new()
    {
        if (IsPrefabAsset)
            return null;

        string? nativeName = GetNativeName<T>();
        if (nativeName == null)
            return null;

        if (!InternalCalls.Entity_AddComponent(ID, nativeName))
            return null;

        return GetComponent<T>();
    }

    public bool HasComponent<T>() where T : Component, new()
    {
        if (IsPrefabAsset)
            return false;

        string? nativeName = GetNativeName<T>();
        return nativeName != null && InternalCalls.Entity_HasComponent(ID, nativeName);
    }

    public bool RemoveComponent<T>() where T : Component, new()
    {
        if (IsPrefabAsset)
            return false;

        string? nativeName = GetNativeName<T>();
        if (nativeName == null) return false;

        bool removed = InternalCalls.Entity_RemoveComponent(ID, nativeName);
        if (removed || !HasComponent<T>())
            InvalidateCachedComponent(typeof(T));

        return removed;
    }

    public T? GetComponent<T>() where T : Component, new()
    {
        if (IsPrefabAsset)
            return null;

        Type type = typeof(T);
        if (!HasComponent<T>())
        {
            InvalidateCachedComponent(type);
            return null;
        }

        if (m_ComponentCache.TryGetValue(type, out Component? cached))
            return cached as T;

        if (!s_NativeComponentNames.ContainsKey(type)
            && s_ManagedComponentStore.TryGetValue((ID, type), out Component? storedComponent))
        {
            m_ComponentCache[type] = storedComponent;
            return storedComponent as T;
        }

        var component = new T { Entity = this };
        if (!s_NativeComponentNames.ContainsKey(type))
        {
            ApplyManagedFieldValues(component, InternalCalls.Entity_GetManagedComponentFields(ID, type.Name));
            s_ManagedComponentStore[(ID, type)] = component;
        }

        m_ComponentCache[type] = component;
        return component;
    }

    private static void ApplyManagedFieldValues(Component component, string json)
    {
        if (string.IsNullOrWhiteSpace(json) || json == "{}")
            return;

        try
        {
            using JsonDocument doc = JsonDocument.Parse(json);
            if (doc.RootElement.ValueKind != JsonValueKind.Object)
                return;

            Type type = component.GetType();
            foreach (JsonProperty property in doc.RootElement.EnumerateObject())
            {
                FieldInfo? field = type.GetField(property.Name, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);
                if (field == null)
                    continue;

                object? value = ParseManagedFieldValue(field.FieldType, property.Value.GetString() ?? "");
                if (value != null)
                    field.SetValue(component, value);
            }
        }
        catch
        {
        }
    }

    private static object? ParseManagedFieldValue(Type type, string value)
    {
        CultureInfo ic = CultureInfo.InvariantCulture;

        if (type == typeof(string)) return value;
        if (type == typeof(bool)) return value == "true" || value == "True" || value == "1";
        if (type == typeof(float) && float.TryParse(value, NumberStyles.Float, ic, out float f)) return f;
        if (type == typeof(double) && double.TryParse(value, NumberStyles.Float, ic, out double d)) return d;
        if (type == typeof(int) && int.TryParse(value, NumberStyles.Integer, ic, out int i)) return i;
        if (type == typeof(uint) && uint.TryParse(value, NumberStyles.Integer, ic, out uint ui)) return ui;
        if (type == typeof(long) && long.TryParse(value, NumberStyles.Integer, ic, out long l)) return l;
        if (type == typeof(ulong) && ulong.TryParse(value, NumberStyles.Integer, ic, out ulong ul)) return ul;
        if (type == typeof(short) && short.TryParse(value, NumberStyles.Integer, ic, out short s)) return s;
        if (type == typeof(ushort) && ushort.TryParse(value, NumberStyles.Integer, ic, out ushort us)) return us;
        if (type == typeof(byte) && byte.TryParse(value, NumberStyles.Integer, ic, out byte b)) return b;
        if (type == typeof(sbyte) && sbyte.TryParse(value, NumberStyles.Integer, ic, out sbyte sb)) return sb;
        if (type == typeof(Entity)) return ParseEntityReference(value);
        if (type == typeof(Texture)) return Texture.FromAssetUUID(ParseAssetUUID(value));
        if (type == typeof(Audio)) return Audio.FromAssetUUID(ParseAssetUUID(value));
        if (type == typeof(TextureRef)) return new TextureRef(ParseAssetUUID(value));
        if (type == typeof(AudioRef)) return new AudioRef(ParseAssetUUID(value));

        string[] parts = value.Split(',', StringSplitOptions.TrimEntries);
        if (type == typeof(Vector2) && parts.Length >= 2)
            return new Vector2(float.Parse(parts[0], ic), float.Parse(parts[1], ic));
        if (type == typeof(Vector2Int) && parts.Length >= 2)
            return new Vector2Int(int.Parse(parts[0], ic), int.Parse(parts[1], ic));
        if (type == typeof(Vector3) && parts.Length >= 3)
            return new Vector3(float.Parse(parts[0], ic), float.Parse(parts[1], ic), float.Parse(parts[2], ic));
        if (type == typeof(Vector3Int) && parts.Length >= 3)
            return new Vector3Int(int.Parse(parts[0], ic), int.Parse(parts[1], ic), int.Parse(parts[2], ic));
        if (type == typeof(Vector4) && parts.Length >= 4)
            return new Vector4(float.Parse(parts[0], ic), float.Parse(parts[1], ic), float.Parse(parts[2], ic), float.Parse(parts[3], ic));
        if (type == typeof(Vector4Int) && parts.Length >= 4)
            return new Vector4Int(int.Parse(parts[0], ic), int.Parse(parts[1], ic), int.Parse(parts[2], ic), int.Parse(parts[3], ic));
        if (type == typeof(Color) && parts.Length >= 4)
            return new Color(float.Parse(parts[0], ic), float.Parse(parts[1], ic), float.Parse(parts[2], ic), float.Parse(parts[3], ic));

        return null;
    }

    internal static ulong ParseAssetUUID(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
            return 0;

        if (ulong.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out ulong assetId))
            return assetId;

        return InternalCalls.Asset_GetOrCreateUUIDFromPath(value);
    }

    internal static Entity ParseEntityReference(string value)
    {
        if (string.IsNullOrWhiteSpace(value) || value == "0")
            return Invalid;

        if (value.StartsWith("prefab:", StringComparison.OrdinalIgnoreCase)
            && ulong.TryParse(value.AsSpan("prefab:".Length), NumberStyles.None, CultureInfo.InvariantCulture, out ulong prefabGuid))
        {
            return FromPrefabGUID(prefabGuid);
        }

        return ulong.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out ulong id) && id != 0
            ? new Entity(id)
            : Invalid;
    }

    public static Entity? FindByName(string name)
    {
        ulong id = InternalCalls.Entity_FindByName(name);
        return id != 0 ? new Entity(id) : null;
    }

    public static Entity Create(string name)
    {
        ulong id = InternalCalls.Entity_Create(name);
        return id != 0 ? new Entity(id) : Invalid;
    }

    public static Entity Instantiate(Entity source)
    {
        if (source is null || source == Invalid)
            return Invalid;

        ulong id = source.IsPrefabAsset
            ? InternalCalls.Entity_InstantiatePrefab(source.PrefabGUID)
            : InternalCalls.Entity_Clone(source.ID);

        return id != 0 ? new Entity(id) : Invalid;
    }

    public static Entity Create(Entity source) => Instantiate(source);

    public Entity Clone() => Instantiate(this);

    public void Destroy()
    {
        if (IsPrefabAsset)
            return;

        InvalidateAllCachedComponents();
        InternalCalls.Entity_Destroy(ID);
    }

    public bool Equals(Entity? other)
        => other is not null && ID == other.ID && m_PrefabAssetGUID == other.m_PrefabAssetGUID;

    public override bool Equals(object? obj) => obj is Entity other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(ID, m_PrefabAssetGUID);

    public static bool operator ==(Entity? a, Entity? b)
    {
        if (a is null) return b is null;
        return a.Equals(b);
    }

    public static bool operator !=(Entity? a, Entity? b) => !(a == b);

    public static implicit operator bool(Entity? entity)
    {
        if (entity is null || entity == Invalid)
            return false;

        return entity.IsPrefabAsset
            ? InternalCalls.Asset_IsValid(entity.m_PrefabAssetGUID)
            : InternalCalls.Entity_IsValid(entity.ID);
    }
}

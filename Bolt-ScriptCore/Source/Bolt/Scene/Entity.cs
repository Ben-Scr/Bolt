using System;
using System.Collections.Generic;

namespace Bolt
{
    public class Entity : IEquatable<Entity>
    {
        public static readonly Entity Invalid = new(0);

        private readonly Dictionary<Type, Component> m_ComponentCache = new();
        private Transform2DComponent? m_TransformComponent;

        protected Entity() { ID = 0; }
        internal Entity(ulong id) { ID = id; }

        public readonly ulong ID;

        // Maps C# component types to the native ComponentRegistry display names.
        // These must match the names used in BuiltInComponentRegistration.cpp.
        private static readonly Dictionary<Type, string> s_NativeComponentNames = new()
        {
            { typeof(NameComponent),                  "Name" },
            { typeof(Transform2DComponent),           "Transform 2D" },
            { typeof(SpriteRendererComponent),        "Sprite Renderer" },
            { typeof(Camera2DComponent),              "Camera 2D" },
            { typeof(Rigidbody2DComponent),           "Rigidbody 2D" },
            { typeof(BoxCollider2DComponent),         "Box Collider 2D" },
            { typeof(AudioSourceComponent),           "Audio Source" },
            { typeof(BoltBody2DComponent),            "Bolt Body 2D" },
            { typeof(BoltBoxCollider2DComponent),     "Bolt Box Collider 2D" },
            { typeof(BoltCircleCollider2DComponent),  "Bolt Circle Collider 2D" },
            { typeof(ParticleSystem2DComponent),     "Particle System 2D" },
        };

        private static string? GetNativeName<T>() =>
            s_NativeComponentNames.TryGetValue(typeof(T), out string? name) ? name : null;

        internal static string? GetNativeComponentName<T>() where T : Component, new() => GetNativeName<T>();
        internal static bool TryGetNativeComponentName(Type type, out string? name) => s_NativeComponentNames.TryGetValue(type, out name);

        private void InvalidateCachedComponent(Type type)
        {
            if (m_ComponentCache.Remove(type, out Component? cached))
                cached.Invalidate();

            if (type == typeof(Transform2DComponent))
                m_TransformComponent = null;
        }

        private void InvalidateAllCachedComponents()
        {
            foreach (Component component in m_ComponentCache.Values)
                component.Invalidate();

            m_ComponentCache.Clear();
            m_TransformComponent = null;
        }

        public string Name => GetComponent<NameComponent>()?.Name ?? "";

        public Transform2DComponent Transform
        {
            get
            {
                if (m_TransformComponent == null)
                {
                    m_TransformComponent = GetComponent<Transform2DComponent>();
                    if (m_TransformComponent == null)
                        throw new InvalidOperationException("Entity does not have a Transform2DComponent");
                }
                return m_TransformComponent;
            }
            set
            {
                m_TransformComponent = value;
            }
        }

        /// <summary>
        /// Add a component to this entity on the native side.
        /// Returns the managed wrapper, or null if the component type is unknown.
        /// </summary>
        public T? AddComponent<T>() where T : Component, new()
        {
            string? nativeName = GetNativeName<T>();
            if (nativeName == null)
                return null;

            if (!InternalCalls.Entity_AddComponent(ID, nativeName))
                return null;

            return GetComponent<T>();
        }

        /// <summary>
        /// Check whether this entity has a specific component.
        /// </summary>
        public bool HasComponent<T>() where T : Component, new()
        {
            string? nativeName = GetNativeName<T>();
            if (nativeName == null) return false;
            return InternalCalls.Entity_HasComponent(ID, nativeName);
        }

        /// <summary>
        /// Remove a component from this entity.
        /// Returns true if the component was removed.
        /// </summary>
        public bool RemoveComponent<T>() where T : Component, new()
        {
            string? nativeName = GetNativeName<T>();
            if (nativeName == null) return false;

            bool removed = InternalCalls.Entity_RemoveComponent(ID, nativeName);
            if (removed || !HasComponent<T>())
                InvalidateCachedComponent(typeof(T));

            return removed;
        }

        public T? GetComponent<T>() where T : Component, new()
        {
            Type type = typeof(T);
            if (!HasComponent<T>())
            {
                InvalidateCachedComponent(type);
                return null;
            }

            if (m_ComponentCache.TryGetValue(type, out Component? cached))
                return cached as T;

            var component = new T { Entity = this };
            m_ComponentCache[type] = component;
            return component;
        }

        public static Entity? FindByName(string name)
        {
            ulong id = InternalCalls.Entity_FindByName(name);
            return id != 0 ? new Entity(id) : null;
        }

        public static Entity Create(string name)
        {
            ulong id = InternalCalls.Entity_Create(name);
            return new Entity(id);
        }

       
        public static Entity Create(Entity source)
        {
            if (source is null || source == Invalid) return Invalid;
            ulong id = InternalCalls.Entity_Clone(source.ID);
            return id != 0 ? new Entity(id) : Invalid;
        }

        public Entity Clone() => Create(this);

        public void Destroy()
        {
            InvalidateAllCachedComponents();
            InternalCalls.Entity_Destroy(ID);
        }

        public bool Equals(Entity? other) => other is not null && ID == other.ID;
        public override bool Equals(object? obj) => obj is Entity other && Equals(other);
        public override int GetHashCode() => (int)ID;

        public static bool operator ==(Entity? a, Entity? b)
        {
            if (a is null) return b is null;
            return a.Equals(b);
        }

        public static bool operator !=(Entity? a, Entity? b) => !(a == b);

        public static implicit operator bool(Entity? entity)
        {
            if (entity is null || entity == Invalid) return false;
            return InternalCalls.Entity_IsValid(entity.ID);
        }
    }
}

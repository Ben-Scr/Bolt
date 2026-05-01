using Axiom.Interop;

namespace Axiom;
// ── NameComponent ───────────────────────────────────────────────────

public class NameComponent : Component
{
    public string Name
    {
        get => InternalCalls.NameComponent_GetName(RequireComponent<NameComponent>());
        set => InternalCalls.NameComponent_SetName(RequireComponent<NameComponent>(), value);
    }
}

// ── Transform2DComponent ────────────────────────────────────────────

public class Transform2DComponent : Component
{
    public Vector2 Position
    {
        get
        {
            ulong entityId = RequireComponent<Transform2DComponent>();
            InternalCalls.Transform2D_GetPosition(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.Transform2D_SetPosition(RequireComponent<Transform2DComponent>(), value.X, value.Y);
    }

    public float Rotation
    {
        get => InternalCalls.Transform2D_GetRotation(RequireComponent<Transform2DComponent>());
        set => InternalCalls.Transform2D_SetRotation(RequireComponent<Transform2DComponent>(), value);
    }

    public float RotationDegrees
    {
        get => Rotation * Mathf.Rad2Deg;
        set => Rotation = value * Mathf.Deg2Rad;
    }

    public Vector2 Scale
    {
        get
        {
            ulong entityId = RequireComponent<Transform2DComponent>();
            InternalCalls.Transform2D_GetScale(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.Transform2D_SetScale(RequireComponent<Transform2DComponent>(), value.X, value.Y);
    }

    public Vector2 Up
    {
        get => new Vector2(-Mathf.Sin(Rotation), Mathf.Cos(Rotation));
    }

    public Vector2 Down
    {
        get => -Up;
    }

    public Vector2 Left
    {
        get => -Right;
    }
    public Vector2 Right
    {
        get => new Vector2(Mathf.Cos(Rotation), Mathf.Sin(Rotation));
    }
}

// ── SpriteRendererComponent ─────────────────────────────────────────

public class SpriteRendererComponent : Component
{
    public Vector4 Color
    {
        get
        {
            ulong entityId = RequireComponent<SpriteRendererComponent>();
            InternalCalls.SpriteRenderer_GetColor(entityId, out float r, out float g, out float b, out float a);
            return new Color(r, g, b, a);
        }
        set => InternalCalls.SpriteRenderer_SetColor(RequireComponent<SpriteRendererComponent>(), value.X, value.Y, value.Z, value.W);
    }

    public Texture Texture
    {
        get
        {
            ulong assetId = InternalCalls.SpriteRenderer_GetTexture(RequireComponent<SpriteRendererComponent>());
            return Texture.FromAssetUUID(assetId)!;
        }
        set => InternalCalls.SpriteRenderer_SetTexture(RequireComponent<SpriteRendererComponent>(), value?.UUID ?? 0);
    }

    public int SortingOrder
    {
        get => InternalCalls.SpriteRenderer_GetSortingOrder(RequireComponent<SpriteRendererComponent>());
        set => InternalCalls.SpriteRenderer_SetSortingOrder(RequireComponent<SpriteRendererComponent>(), value);
    }

    public int SortingLayer
    {
        get => InternalCalls.SpriteRenderer_GetSortingLayer(RequireComponent<SpriteRendererComponent>());
        set => InternalCalls.SpriteRenderer_SetSortingLayer(RequireComponent<SpriteRendererComponent>(), value);
    }
}

// ── Camera2DComponent ───────────────────────────────────────────────

public class Camera2DComponent : Component
{
    public float OrthographicSize
    {
        get => InternalCalls.Camera2D_GetOrthographicSize(RequireComponent<Camera2DComponent>());
        set => InternalCalls.Camera2D_SetOrthographicSize(RequireComponent<Camera2DComponent>(), value);
    }

    public float Zoom
    {
        get => InternalCalls.Camera2D_GetZoom(RequireComponent<Camera2DComponent>());
        set => InternalCalls.Camera2D_SetZoom(RequireComponent<Camera2DComponent>(), value);
    }

    public static Camera2DComponent Main {
        get
        {
            throw new System.NotImplementedException();
        }
    }

    public Vector4 ClearColor
    {
        get
        {
            ulong entityId = RequireComponent<Camera2DComponent>();
            InternalCalls.Camera2D_GetClearColor(entityId, out float r, out float g, out float b, out float a);
            return new Color(r, g, b, a);
        }
        set => InternalCalls.Camera2D_SetClearColor(RequireComponent<Camera2DComponent>(), value.X, value.Y, value.Z, value.W);
    }

    public Vector2 ScreenToWorld(Vector2 screenPos)
    {
        GetViewportSize(out float viewportWidth, out float viewportHeight);
        if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
            return Vector2.Zero;

        GetCameraBasis(viewportWidth, viewportHeight, out Vector2 position, out float cos, out float sin, out float halfWidth, out float halfHeight);

        float localX = ((2.0f * screenPos.X / viewportWidth) - 1.0f) * halfWidth;
        float localY = (1.0f - (2.0f * screenPos.Y / viewportHeight)) * halfHeight;

        return new Vector2(
            position.X + (cos * localX) - (sin * localY),
            position.Y + (sin * localX) + (cos * localY));
    }

    public Vector2 WorldToScreen(Vector2 worldPos)
    {
        GetViewportSize(out float viewportWidth, out float viewportHeight);
        if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
            return Vector2.Zero;

        GetCameraBasis(viewportWidth, viewportHeight, out Vector2 position, out float cos, out float sin, out float halfWidth, out float halfHeight);
        if (halfWidth <= Mathf.Epsilon || halfHeight <= Mathf.Epsilon)
            return Vector2.Zero;

        Vector2 delta = worldPos - position;
        float localX = (cos * delta.X) + (sin * delta.Y);
        float localY = (-sin * delta.X) + (cos * delta.Y);

        float ndcX = localX / halfWidth;
        float ndcY = localY / halfHeight;

        return new Vector2(
            (ndcX + 1.0f) * 0.5f * viewportWidth,
            (1.0f - ndcY) * 0.5f * viewportHeight);
    }

    public float ViewportWidth => InternalCalls.Camera2D_GetViewportWidth(RequireComponent<Camera2DComponent>());
    public float ViewportHeight => InternalCalls.Camera2D_GetViewportHeight(RequireComponent<Camera2DComponent>());

    private void GetViewportSize(out float viewportWidth, out float viewportHeight)
    {
        ulong entityId = RequireComponent<Camera2DComponent>();
        viewportWidth = InternalCalls.Camera2D_GetViewportWidth(entityId);
        viewportHeight = InternalCalls.Camera2D_GetViewportHeight(entityId);
    }

    private void GetCameraBasis(float viewportWidth, float viewportHeight, out Vector2 position, out float cos, out float sin, out float halfWidth, out float halfHeight)
    {
        Transform2DComponent transform = Entity.Transform;
        position = transform.Position;

        float rotation = transform.Rotation;
        cos = Mathf.Cos(rotation);
        sin = Mathf.Sin(rotation);

        halfHeight = OrthographicSize * Zoom;
        halfWidth = halfHeight * (viewportWidth / viewportHeight);
    }
}

// ── Rigidbody2DComponent ────────────────────────────────────────────

public enum BodyType { Static = 0, Kinematic = 1, Dynamic = 2 }

public class Rigidbody2DComponent : Component
{
    public Vector2 LinearVelocity
    {
        get
        {
            ulong entityId = RequireComponent<Rigidbody2DComponent>();
            InternalCalls.Rigidbody2D_GetLinearVelocity(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.Rigidbody2D_SetLinearVelocity(RequireComponent<Rigidbody2DComponent>(), value.X, value.Y);
    }

    public float AngularVelocity
    {
        get => InternalCalls.Rigidbody2D_GetAngularVelocity(RequireComponent<Rigidbody2DComponent>());
        set => InternalCalls.Rigidbody2D_SetAngularVelocity(RequireComponent<Rigidbody2DComponent>(), value);
    }

    public BodyType BodyType
    {
        get => (BodyType)InternalCalls.Rigidbody2D_GetBodyType(RequireComponent<Rigidbody2DComponent>());
        set => InternalCalls.Rigidbody2D_SetBodyType(RequireComponent<Rigidbody2DComponent>(), (int)value);
    }

    public float GravityScale
    {
        get => InternalCalls.Rigidbody2D_GetGravityScale(RequireComponent<Rigidbody2DComponent>());
        set => InternalCalls.Rigidbody2D_SetGravityScale(RequireComponent<Rigidbody2DComponent>(), value);
    }

    public float Mass
    {
        get => InternalCalls.Rigidbody2D_GetMass(RequireComponent<Rigidbody2DComponent>());
        set => InternalCalls.Rigidbody2D_SetMass(RequireComponent<Rigidbody2DComponent>(), value);
    }

    public void ApplyForce(Vector2 force, bool wake = true)
        => InternalCalls.Rigidbody2D_ApplyForce(RequireComponent<Rigidbody2DComponent>(), force.X, force.Y, wake);

    public void ApplyImpulse(Vector2 impulse, bool wake = true)
        => InternalCalls.Rigidbody2D_ApplyImpulse(RequireComponent<Rigidbody2DComponent>(), impulse.X, impulse.Y, wake);
}

// ── BoxCollider2DComponent ──────────────────────────────────────────

public class BoxCollider2DComponent : Component
{
    public Vector2 Scale
    {
        get
        {
            ulong entityId = RequireComponent<BoxCollider2DComponent>();
            InternalCalls.BoxCollider2D_GetScale(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
    }

    public Vector2 Center
    {
        get
        {
            ulong entityId = RequireComponent<BoxCollider2DComponent>();
            InternalCalls.BoxCollider2D_GetCenter(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
    }

    public bool Enabled
    {
        set => InternalCalls.BoxCollider2D_SetEnabled(RequireComponent<BoxCollider2DComponent>(), value);
    }
}

// ── AudioSourceComponent ────────────────────────────────────────────

public class AudioSourceComponent : Component
{
    public float Volume
    {
        get => InternalCalls.AudioSource_GetVolume(RequireComponent<AudioSourceComponent>());
        set => InternalCalls.AudioSource_SetVolume(RequireComponent<AudioSourceComponent>(), value);
    }

    public float Pitch
    {
        get => InternalCalls.AudioSource_GetPitch(RequireComponent<AudioSourceComponent>());
        set => InternalCalls.AudioSource_SetPitch(RequireComponent<AudioSourceComponent>(), value);
    }

    public bool Loop
    {
        get => InternalCalls.AudioSource_GetLoop(RequireComponent<AudioSourceComponent>());
        set => InternalCalls.AudioSource_SetLoop(RequireComponent<AudioSourceComponent>(), value);
    }

    public bool IsPlaying => InternalCalls.AudioSource_IsPlaying(RequireComponent<AudioSourceComponent>());
    public bool IsPaused => InternalCalls.AudioSource_IsPaused(RequireComponent<AudioSourceComponent>());

    public void Play() => InternalCalls.AudioSource_Play(RequireComponent<AudioSourceComponent>());
    public void Pause() => InternalCalls.AudioSource_Pause(RequireComponent<AudioSourceComponent>());
    public void Stop() => InternalCalls.AudioSource_Stop(RequireComponent<AudioSourceComponent>());
    public void Resume() => InternalCalls.AudioSource_Resume(RequireComponent<AudioSourceComponent>());
}

// ── ParticleSystem2DComponent ─────────────────────────────────────────

public class ParticleSystem2DComponent : Component
{
    public bool PlayOnAwake
    {
        get => InternalCalls.ParticleSystem2D_GetPlayOnAwake(RequireComponent<ParticleSystem2DComponent>());
        set => InternalCalls.ParticleSystem2D_SetPlayOnAwake(RequireComponent<ParticleSystem2DComponent>(), value);
    }

    public bool IsPlaying => InternalCalls.ParticleSystem2D_IsPlaying(RequireComponent<ParticleSystem2DComponent>());

    public Vector4 Color
    {
        get
        {
            ulong entityId = RequireComponent<ParticleSystem2DComponent>();
            InternalCalls.ParticleSystem2D_GetColor(entityId, out float r, out float g, out float b, out float a);
            return new Color(r, g, b, a);
        }
        set => InternalCalls.ParticleSystem2D_SetColor(RequireComponent<ParticleSystem2DComponent>(), value.X, value.Y, value.Z, value.W);
    }

    public float LifeTime
    {
        get => InternalCalls.ParticleSystem2D_GetLifeTime(RequireComponent<ParticleSystem2DComponent>());
        set => InternalCalls.ParticleSystem2D_SetLifeTime(RequireComponent<ParticleSystem2DComponent>(), value);
    }

    public float Speed
    {
        get => InternalCalls.ParticleSystem2D_GetSpeed(RequireComponent<ParticleSystem2DComponent>());
        set => InternalCalls.ParticleSystem2D_SetSpeed(RequireComponent<ParticleSystem2DComponent>(), value);
    }

    public float Scale
    {
        get => InternalCalls.ParticleSystem2D_GetScale(RequireComponent<ParticleSystem2DComponent>());
        set => InternalCalls.ParticleSystem2D_SetScale(RequireComponent<ParticleSystem2DComponent>(), value);
    }

    public int EmitOverTime
    {
        get => InternalCalls.ParticleSystem2D_GetEmitOverTime(RequireComponent<ParticleSystem2DComponent>());
        set => InternalCalls.ParticleSystem2D_SetEmitOverTime(RequireComponent<ParticleSystem2DComponent>(), value);
    }

    public void Play() => InternalCalls.ParticleSystem2D_Play(RequireComponent<ParticleSystem2DComponent>());
    public void Pause() => InternalCalls.ParticleSystem2D_Pause(RequireComponent<ParticleSystem2DComponent>());
    public void Stop() => InternalCalls.ParticleSystem2D_Stop(RequireComponent<ParticleSystem2DComponent>());
    public void Emit(int count) => InternalCalls.ParticleSystem2D_Emit(RequireComponent<ParticleSystem2DComponent>(), count);
}

// ── Axiom-Physics Components ─────────────────────────────────────────
// These use the lightweight Axiom-Physics engine (AABB-based collision).
// For full physics (rotation, friction, CCD), use Rigidbody2DComponent
// and BoxCollider2DComponent instead (Box2D-backed).

public enum FastBodyType { Static = 0, Dynamic = 1, Kinematic = 2 }

public class FastBody2DComponent : Component
{
    public FastBodyType BodyType
    {
        get => (FastBodyType)InternalCalls.FastBody2D_GetBodyType(RequireComponent<FastBody2DComponent>());
        set => InternalCalls.FastBody2D_SetBodyType(RequireComponent<FastBody2DComponent>(), (int)value);
    }

    public float Mass
    {
        get => InternalCalls.FastBody2D_GetMass(RequireComponent<FastBody2DComponent>());
        set => InternalCalls.FastBody2D_SetMass(RequireComponent<FastBody2DComponent>(), value);
    }

    public bool UseGravity
    {
        get => InternalCalls.FastBody2D_GetUseGravity(RequireComponent<FastBody2DComponent>());
        set => InternalCalls.FastBody2D_SetUseGravity(RequireComponent<FastBody2DComponent>(), value);
    }

    public Vector2 Velocity
    {
        get
        {
            ulong entityId = RequireComponent<FastBody2DComponent>();
            InternalCalls.FastBody2D_GetVelocity(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.FastBody2D_SetVelocity(RequireComponent<FastBody2DComponent>(), value.X, value.Y);
    }
}

public class FastBoxCollider2DComponent : Component
{
    public Vector2 HalfExtents
    {
        get
        {
            ulong entityId = RequireComponent<FastBoxCollider2DComponent>();
            InternalCalls.FastBoxCollider2D_GetHalfExtents(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.FastBoxCollider2D_SetHalfExtents(RequireComponent<FastBoxCollider2DComponent>(), value.X, value.Y);
    }
}

public class FastCircleCollider2DComponent : Component
{
    public float Radius
    {
        get => InternalCalls.FastCircleCollider2D_GetRadius(RequireComponent<FastCircleCollider2DComponent>());
        set => InternalCalls.FastCircleCollider2D_SetRadius(RequireComponent<FastCircleCollider2DComponent>(), value);
    }
}

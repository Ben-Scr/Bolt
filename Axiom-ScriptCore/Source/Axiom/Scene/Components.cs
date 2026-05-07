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

// ── Transform2D ─────────────────────────────────────────────────────

public class Transform2D : Component
{
    public Vector2 Position
    {
        get
        {
            ulong entityId = RequireComponent<Transform2D>();
            InternalCalls.Transform2D_GetPosition(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.Transform2D_SetPosition(RequireComponent<Transform2D>(), value.X, value.Y);
    }

    public float Rotation
    {
        get => InternalCalls.Transform2D_GetRotation(RequireComponent<Transform2D>());
        set => InternalCalls.Transform2D_SetRotation(RequireComponent<Transform2D>(), value);
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
            ulong entityId = RequireComponent<Transform2D>();
            InternalCalls.Transform2D_GetScale(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.Transform2D_SetScale(RequireComponent<Transform2D>(), value.X, value.Y);
    }

    public Vector2 LocalTransform
    {
        get
        {
            ulong entityId = RequireComponent<Transform2D>();
            InternalCalls.Transform2D_GetLocalPosition(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.Transform2D_SetLocalPosition(RequireComponent<Transform2D>(), value.X, value.Y);
    }
    public float LocalRotation
    {
        get => InternalCalls.Transform2D_GetLocalRotation(RequireComponent<Transform2D>());
        set => InternalCalls.Transform2D_SetLocalRotation(RequireComponent<Transform2D>(), value);
    }
    public Vector2 LocalScale
    {
        get
        {
            ulong entityId = RequireComponent<Transform2D>();
            InternalCalls.Transform2D_GetLocalScale(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.Transform2D_SetLocalScale(RequireComponent<Transform2D>(), value.X, value.Y);
    }

    public new Entity? Entity
    {
        get
        {
            ulong entityId = InternalCalls.Transform2D_GetEntity(RequireComponent<Transform2D>());
            return entityId != 0 ? new Entity(entityId) : null;
        }
    }

    public Transform2D? Child => GetChildAt(0);

    public Transform2D? Parent
    {
        get
        {
            ulong parentId = InternalCalls.Transform2D_GetParent(RequireComponent<Transform2D>());
            return parentId != 0 ? new Entity(parentId).GetComponent<Transform2D>() : null;
        }
    }

    public int ChildCount => InternalCalls.Transform2D_GetChildCount(RequireComponent<Transform2D>());

    public Transform2D[] GetChilds()
    {
        ulong entityId = RequireComponent<Transform2D>();
        int count = InternalCalls.Transform2D_GetChildCount(entityId);
        if (count <= 0) return Array.Empty<Transform2D>();

        ulong[] ids = new ulong[count];
        int actual = InternalCalls.Transform2D_GetChildren(entityId, ids);

        var result = new List<Transform2D>(actual);
        for (int i = 0; i < actual; i++)
        {
            if (ids[i] == 0) continue;
            Transform2D? child = new Entity(ids[i]).GetComponent<Transform2D>();
            if (child != null) result.Add(child);
        }
        return result.ToArray();
    }

    public Transform2D? GetChildAt(int index)
    {
        if (index < 0) return null;
        ulong childId = InternalCalls.Transform2D_GetChildAt(RequireComponent<Transform2D>(), index);
        return childId != 0 ? new Entity(childId).GetComponent<Transform2D>() : null;
    }

    public bool SetParent(Transform2D? newParent)
    {
        ulong entityId = RequireComponent<Transform2D>();
        ulong parentId = newParent?.RequireComponent<Transform2D>() ?? 0;
        return InternalCalls.Transform2D_SetParent(entityId, parentId);
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

// ── SpriteRenderer ──────────────────────────────────────────────────

public class SpriteRenderer : Component
{
    public Vector4 Color
    {
        get
        {
            ulong entityId = RequireComponent<SpriteRenderer>();
            InternalCalls.SpriteRenderer_GetColor(entityId, out float r, out float g, out float b, out float a);
            return new Color(r, g, b, a);
        }
        set => InternalCalls.SpriteRenderer_SetColor(RequireComponent<SpriteRenderer>(), value.X, value.Y, value.Z, value.W);
    }

    public Texture Texture
    {
        get
        {
            ulong assetId = InternalCalls.SpriteRenderer_GetTexture(RequireComponent<SpriteRenderer>());
            return Texture.FromAssetUUID(assetId)!;
        }
        set => InternalCalls.SpriteRenderer_SetTexture(RequireComponent<SpriteRenderer>(), value?.UUID ?? 0);
    }

    public int SortingOrder
    {
        get => InternalCalls.SpriteRenderer_GetSortingOrder(RequireComponent<SpriteRenderer>());
        set => InternalCalls.SpriteRenderer_SetSortingOrder(RequireComponent<SpriteRenderer>(), value);
    }

    public int SortingLayer
    {
        get => InternalCalls.SpriteRenderer_GetSortingLayer(RequireComponent<SpriteRenderer>());
        set => InternalCalls.SpriteRenderer_SetSortingLayer(RequireComponent<SpriteRenderer>(), value);
    }
}


// ── TextRenderer ────────────────────────────────────────────────────

public enum TextAlignment { Left = 0, Center = 1, Right = 2 }

public class TextRenderer : Component
{
    public string Text
    {
        get => InternalCalls.TextRenderer_GetText(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetText(RequireComponent<TextRenderer>(), value ?? "");
    }

    // Returns null when the entity references a missing or invalid font asset.
    public Font? Font
    {
        get
        {
            ulong assetId = InternalCalls.TextRenderer_GetFont(RequireComponent<TextRenderer>());
            return Font.FromAssetUUID(assetId);
        }
        set => InternalCalls.TextRenderer_SetFont(RequireComponent<TextRenderer>(), value?.UUID ?? 0);
    }

    public float FontSize
    {
        get => InternalCalls.TextRenderer_GetFontSize(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetFontSize(RequireComponent<TextRenderer>(), value);
    }

    public Vector4 Color
    {
        get
        {
            ulong entityId = RequireComponent<TextRenderer>();
            InternalCalls.TextRenderer_GetColor(entityId, out float r, out float g, out float b, out float a);
            return new Color(r, g, b, a);
        }
        set => InternalCalls.TextRenderer_SetColor(RequireComponent<TextRenderer>(), value.X, value.Y, value.Z, value.W);
    }

    public float LetterSpacing
    {
        get => InternalCalls.TextRenderer_GetLetterSpacing(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetLetterSpacing(RequireComponent<TextRenderer>(), value);
    }

    public TextAlignment Alignment
    {
        get => (TextAlignment)InternalCalls.TextRenderer_GetHAlign(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetHAlign(RequireComponent<TextRenderer>(), (int)value);
    }

    public int SortingOrder
    {
        get => InternalCalls.TextRenderer_GetSortingOrder(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetSortingOrder(RequireComponent<TextRenderer>(), value);
    }

    public int SortingLayer
    {
        get => InternalCalls.TextRenderer_GetSortingLayer(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetSortingLayer(RequireComponent<TextRenderer>(), value);
    }
}

// ── Camera2D ────────────────────────────────────────────────────────

public class Camera2D : Component
{
    public float OrthographicSize
    {
        get => InternalCalls.Camera2D_GetOrthographicSize(RequireComponent<Camera2D>());
        set => InternalCalls.Camera2D_SetOrthographicSize(RequireComponent<Camera2D>(), value);
    }

    public float Zoom
    {
        get => InternalCalls.Camera2D_GetZoom(RequireComponent<Camera2D>());
        set => InternalCalls.Camera2D_SetZoom(RequireComponent<Camera2D>(), value);
    }

    // Returns the active scene's main Camera2D, or null when no
    // enabled camera exists. Resolves to the entity backing
    // `Scene::GetMainCameraEntity()` on the native side, then wraps it through
    // the standard `Entity.GetComponent<Camera2D>()` lookup so the
    // returned component shares the entity's component cache and is
    // invalidated alongside other component references.
    public static Camera2D? Main
    {
        get
        {
            ulong entityId = InternalCalls.Camera2D_GetMainEntity();
            if (entityId == 0) return null;
            return new Entity(entityId).GetComponent<Camera2D>();
        }
    }

    public Vector4 ClearColor
    {
        get
        {
            ulong entityId = RequireComponent<Camera2D>();
            InternalCalls.Camera2D_GetClearColor(entityId, out float r, out float g, out float b, out float a);
            return new Color(r, g, b, a);
        }
        set => InternalCalls.Camera2D_SetClearColor(RequireComponent<Camera2D>(), value.X, value.Y, value.Z, value.W);
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

    public float ViewportWidth => InternalCalls.Camera2D_GetViewportWidth(RequireComponent<Camera2D>());
    public float ViewportHeight => InternalCalls.Camera2D_GetViewportHeight(RequireComponent<Camera2D>());

    private void GetViewportSize(out float viewportWidth, out float viewportHeight)
    {
        ulong entityId = RequireComponent<Camera2D>();
        viewportWidth = InternalCalls.Camera2D_GetViewportWidth(entityId);
        viewportHeight = InternalCalls.Camera2D_GetViewportHeight(entityId);
    }

    private void GetCameraBasis(float viewportWidth, float viewportHeight, out Vector2 position, out float cos, out float sin, out float halfWidth, out float halfHeight)
    {
        Transform2D transform = Entity.Transform;
        position = transform.Position;

        float rotation = transform.Rotation;
        cos = Mathf.Cos(rotation);
        sin = Mathf.Sin(rotation);

        halfHeight = OrthographicSize * Zoom;
        halfWidth = halfHeight * (viewportWidth / viewportHeight);
    }
}

// ── Rigidbody2D ─────────────────────────────────────────────────────

public enum BodyType { Static = 0, Kinematic = 1, Dynamic = 2 }

public class Rigidbody2D : Component
{
    public Vector2 LinearVelocity
    {
        get
        {
            ulong entityId = RequireComponent<Rigidbody2D>();
            InternalCalls.Rigidbody2D_GetLinearVelocity(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.Rigidbody2D_SetLinearVelocity(RequireComponent<Rigidbody2D>(), value.X, value.Y);
    }

    public float AngularVelocity
    {
        get => InternalCalls.Rigidbody2D_GetAngularVelocity(RequireComponent<Rigidbody2D>());
        set => InternalCalls.Rigidbody2D_SetAngularVelocity(RequireComponent<Rigidbody2D>(), value);
    }

    public BodyType BodyType
    {
        get => (BodyType)InternalCalls.Rigidbody2D_GetBodyType(RequireComponent<Rigidbody2D>());
        set => InternalCalls.Rigidbody2D_SetBodyType(RequireComponent<Rigidbody2D>(), (int)value);
    }

    public float GravityScale
    {
        get => InternalCalls.Rigidbody2D_GetGravityScale(RequireComponent<Rigidbody2D>());
        set => InternalCalls.Rigidbody2D_SetGravityScale(RequireComponent<Rigidbody2D>(), value);
    }

    public float Mass
    {
        get => InternalCalls.Rigidbody2D_GetMass(RequireComponent<Rigidbody2D>());
        set => InternalCalls.Rigidbody2D_SetMass(RequireComponent<Rigidbody2D>(), value);
    }

    public void ApplyForce(Vector2 force, bool wake = true)
        => InternalCalls.Rigidbody2D_ApplyForce(RequireComponent<Rigidbody2D>(), force.X, force.Y, wake);

    public void ApplyImpulse(Vector2 impulse, bool wake = true)
        => InternalCalls.Rigidbody2D_ApplyImpulse(RequireComponent<Rigidbody2D>(), impulse.X, impulse.Y, wake);
}

// ── BoxCollider2D ───────────────────────────────────────────────────

public class BoxCollider2D : Component
{
    public Vector2 Scale
    {
        get
        {
            ulong entityId = RequireComponent<BoxCollider2D>();
            InternalCalls.BoxCollider2D_GetScale(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
    }

    public Vector2 Center
    {
        get
        {
            ulong entityId = RequireComponent<BoxCollider2D>();
            InternalCalls.BoxCollider2D_GetCenter(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
    }

    public bool Enabled
    {
        set => InternalCalls.BoxCollider2D_SetEnabled(RequireComponent<BoxCollider2D>(), value);
    }
}

// ── AudioSource ─────────────────────────────────────────────────────

public class AudioSource : Component
{
    // Audio asset assigned to this source. Mirrors the
    // `SpriteRenderer.Texture` pattern: round-trips the asset UUID
    // through the native binding, returns null when no clip is assigned or
    // the UUID resolves to an asset that doesn't actually exist on disk.
    // Setting null clears the assignment.
    public Audio? Audio
    {
        get
        {
            ulong assetId = InternalCalls.AudioSource_GetAudio(RequireComponent<AudioSource>());
            return Axiom.Audio.FromAssetUUID(assetId);
        }
        set => InternalCalls.AudioSource_SetAudio(RequireComponent<AudioSource>(), value?.UUID ?? 0);
    }

    public float Volume
    {
        get => InternalCalls.AudioSource_GetVolume(RequireComponent<AudioSource>());
        set => InternalCalls.AudioSource_SetVolume(RequireComponent<AudioSource>(), value);
    }

    public float Pitch
    {
        get => InternalCalls.AudioSource_GetPitch(RequireComponent<AudioSource>());
        set => InternalCalls.AudioSource_SetPitch(RequireComponent<AudioSource>(), value);
    }

    public bool Loop
    {
        get => InternalCalls.AudioSource_GetLoop(RequireComponent<AudioSource>());
        set => InternalCalls.AudioSource_SetLoop(RequireComponent<AudioSource>(), value);
    }

    public bool IsPlaying => InternalCalls.AudioSource_IsPlaying(RequireComponent<AudioSource>());
    public bool IsPaused => InternalCalls.AudioSource_IsPaused(RequireComponent<AudioSource>());

    public void Play() => InternalCalls.AudioSource_Play(RequireComponent<AudioSource>());
    public void Pause() => InternalCalls.AudioSource_Pause(RequireComponent<AudioSource>());
    public void Stop() => InternalCalls.AudioSource_Stop(RequireComponent<AudioSource>());
    public void Resume() => InternalCalls.AudioSource_Resume(RequireComponent<AudioSource>());
}

// ── ParticleSystem2D ──────────────────────────────────────────────────

public class ParticleSystem2D : Component
{
    public bool PlayOnAwake
    {
        get => InternalCalls.ParticleSystem2D_GetPlayOnAwake(RequireComponent<ParticleSystem2D>());
        set => InternalCalls.ParticleSystem2D_SetPlayOnAwake(RequireComponent<ParticleSystem2D>(), value);
    }

    public bool IsPlaying => InternalCalls.ParticleSystem2D_IsPlaying(RequireComponent<ParticleSystem2D>());

    public Vector4 Color
    {
        get
        {
            ulong entityId = RequireComponent<ParticleSystem2D>();
            InternalCalls.ParticleSystem2D_GetColor(entityId, out float r, out float g, out float b, out float a);
            return new Color(r, g, b, a);
        }
        set => InternalCalls.ParticleSystem2D_SetColor(RequireComponent<ParticleSystem2D>(), value.X, value.Y, value.Z, value.W);
    }

    public float LifeTime
    {
        get => InternalCalls.ParticleSystem2D_GetLifeTime(RequireComponent<ParticleSystem2D>());
        set => InternalCalls.ParticleSystem2D_SetLifeTime(RequireComponent<ParticleSystem2D>(), value);
    }

    public float Speed
    {
        get => InternalCalls.ParticleSystem2D_GetSpeed(RequireComponent<ParticleSystem2D>());
        set => InternalCalls.ParticleSystem2D_SetSpeed(RequireComponent<ParticleSystem2D>(), value);
    }

    public float Scale
    {
        get => InternalCalls.ParticleSystem2D_GetScale(RequireComponent<ParticleSystem2D>());
        set => InternalCalls.ParticleSystem2D_SetScale(RequireComponent<ParticleSystem2D>(), value);
    }

    public int EmitOverTime
    {
        get => InternalCalls.ParticleSystem2D_GetEmitOverTime(RequireComponent<ParticleSystem2D>());
        set => InternalCalls.ParticleSystem2D_SetEmitOverTime(RequireComponent<ParticleSystem2D>(), value);
    }

    public void Play() => InternalCalls.ParticleSystem2D_Play(RequireComponent<ParticleSystem2D>());
    public void Pause() => InternalCalls.ParticleSystem2D_Pause(RequireComponent<ParticleSystem2D>());
    public void Stop() => InternalCalls.ParticleSystem2D_Stop(RequireComponent<ParticleSystem2D>());
    public void Emit(int count) => InternalCalls.ParticleSystem2D_Emit(RequireComponent<ParticleSystem2D>(), count);
}

// ── Axiom-Physics Components ─────────────────────────────────────────
// These use the lightweight Axiom-Physics engine (AABB-based collision).
// For full physics (rotation, friction, CCD), use Rigidbody2D and
// BoxCollider2D instead (Box2D-backed).

public enum FastBodyType { Static = 0, Dynamic = 1, Kinematic = 2 }

public class FastBody2D : Component
{
    public FastBodyType BodyType
    {
        get => (FastBodyType)InternalCalls.FastBody2D_GetBodyType(RequireComponent<FastBody2D>());
        set => InternalCalls.FastBody2D_SetBodyType(RequireComponent<FastBody2D>(), (int)value);
    }

    public float Mass
    {
        get => InternalCalls.FastBody2D_GetMass(RequireComponent<FastBody2D>());
        set => InternalCalls.FastBody2D_SetMass(RequireComponent<FastBody2D>(), value);
    }

    public bool UseGravity
    {
        get => InternalCalls.FastBody2D_GetUseGravity(RequireComponent<FastBody2D>());
        set => InternalCalls.FastBody2D_SetUseGravity(RequireComponent<FastBody2D>(), value);
    }

    public Vector2 Velocity
    {
        get
        {
            ulong entityId = RequireComponent<FastBody2D>();
            InternalCalls.FastBody2D_GetVelocity(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.FastBody2D_SetVelocity(RequireComponent<FastBody2D>(), value.X, value.Y);
    }
}

public class FastBoxCollider2D : Component
{
    public Vector2 HalfExtents
    {
        get
        {
            ulong entityId = RequireComponent<FastBoxCollider2D>();
            InternalCalls.FastBoxCollider2D_GetHalfExtents(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.FastBoxCollider2D_SetHalfExtents(RequireComponent<FastBoxCollider2D>(), value.X, value.Y);
    }
}

public class FastCircleCollider2D : Component
{
    public float Radius
    {
        get => InternalCalls.FastCircleCollider2D_GetRadius(RequireComponent<FastCircleCollider2D>());
        set => InternalCalls.FastCircleCollider2D_SetRadius(RequireComponent<FastCircleCollider2D>(), value);
    }
}

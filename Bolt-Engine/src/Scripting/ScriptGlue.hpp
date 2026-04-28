#pragma once
#include <cstdint>

namespace Bolt {

	/// Layout must match C# NativeBindingsStruct exactly (Sequential, blittable).
	struct NativeBindings
	{
		// ── Application ──────────────────────────────────────────────
		float  (*Application_GetDeltaTime)();
		float  (*Application_GetElapsedTime)();
		int    (*Application_GetScreenWidth)();
		int    (*Application_GetScreenHeight)();
		float  (*Application_GetTargetFrameRate)();
		void   (*Application_SetTargetFrameRate)(float fps);
		void   (*Application_Quit)();
		float  (*Application_GetFixedDeltaTime)();
		float  (*Application_GetUnscaledDeltaTime)();
		float  (*Application_GetFixedUnscaledDeltaTime)();

		// ── Log ──────────────────────────────────────────────────────
		void   (*Log_Trace)(const char* message);
		void   (*Log_Info)(const char* message);
		void   (*Log_Warn)(const char* message);
		void   (*Log_Error)(const char* message);

		// ── Input ────────────────────────────────────────────────────
		int    (*Input_GetKey)(int keyCode);
		int    (*Input_GetKeyDown)(int keyCode);
		int    (*Input_GetKeyUp)(int keyCode);
		int    (*Input_GetMouseButton)(int button);
		int    (*Input_GetMouseButtonDown)(int button);
		int    (*Input_GetMouseButtonUp)(int button);
		void   (*Input_GetMousePosition)(float* outX, float* outY);
		void   (*Input_GetAxis)(float* outX, float* outY);
		void   (*Input_GetMouseDelta)(float* outX, float* outY);
		float  (*Input_GetScrollWheelDelta)();

		// ── Entity ───────────────────────────────────────────────────
		int      (*Entity_IsValid)(uint64_t entityID);
		uint64_t (*Entity_FindByName)(const char* name);
		void     (*Entity_Destroy)(uint64_t entityID);
		uint64_t (*Entity_Create)(const char* name);
		uint64_t (*Entity_Clone)(uint64_t sourceEntityID);
		int      (*Entity_HasComponent)(uint64_t entityID, const char* componentName);
		int      (*Entity_AddComponent)(uint64_t entityID, const char* componentName);
		int      (*Entity_RemoveComponent)(uint64_t entityID, const char* componentName);

		// ── NameComponent ────────────────────────────────────────────
		const char* (*NameComponent_GetName)(uint64_t entityID);
		void        (*NameComponent_SetName)(uint64_t entityID, const char* name);

		// ── Transform2D ──────────────────────────────────────────────
		void  (*Transform2D_GetPosition)(uint64_t entityID, float* outX, float* outY);
		void  (*Transform2D_SetPosition)(uint64_t entityID, float x, float y);
		float (*Transform2D_GetRotation)(uint64_t entityID);
		void  (*Transform2D_SetRotation)(uint64_t entityID, float rotation);
		void  (*Transform2D_GetScale)(uint64_t entityID, float* outX, float* outY);
		void  (*Transform2D_SetScale)(uint64_t entityID, float x, float y);

		// ── SpriteRenderer ───────────────────────────────────────────
		void (*SpriteRenderer_GetColor)(uint64_t entityID, float* r, float* g, float* b, float* a);
		void (*SpriteRenderer_SetColor)(uint64_t entityID, float r, float g, float b, float a);
		uint64_t (*SpriteRenderer_GetTexture)(uint64_t entityID);
		void (*SpriteRenderer_SetTexture)(uint64_t entityID, uint64_t assetId);
		int  (*SpriteRenderer_GetSortingOrder)(uint64_t entityID);
		void (*SpriteRenderer_SetSortingOrder)(uint64_t entityID, int order);
		int  (*SpriteRenderer_GetSortingLayer)(uint64_t entityID);
		void (*SpriteRenderer_SetSortingLayer)(uint64_t entityID, int layer);

		// ── Camera2D ─────────────────────────────────────────────────
		float (*Camera2D_GetOrthographicSize)(uint64_t entityID);
		void  (*Camera2D_SetOrthographicSize)(uint64_t entityID, float size);
		float (*Camera2D_GetZoom)(uint64_t entityID);
		void  (*Camera2D_SetZoom)(uint64_t entityID, float zoom);
		void  (*Camera2D_GetClearColor)(uint64_t entityID, float* r, float* g, float* b, float* a);
		void  (*Camera2D_SetClearColor)(uint64_t entityID, float r, float g, float b, float a);
		void  (*Camera2D_ScreenToWorld)(uint64_t entityID, float sx, float sy, float* outX, float* outY);
		float (*Camera2D_GetViewportWidth)(uint64_t entityID);
		float (*Camera2D_GetViewportHeight)(uint64_t entityID);

		// ── Rigidbody2D ──────────────────────────────────────────────
		void  (*Rigidbody2D_ApplyForce)(uint64_t entityID, float forceX, float forceY, int wake);
		void  (*Rigidbody2D_ApplyImpulse)(uint64_t entityID, float impulseX, float impulseY, int wake);
		void  (*Rigidbody2D_GetLinearVelocity)(uint64_t entityID, float* outX, float* outY);
		void  (*Rigidbody2D_SetLinearVelocity)(uint64_t entityID, float x, float y);
		float (*Rigidbody2D_GetAngularVelocity)(uint64_t entityID);
		void  (*Rigidbody2D_SetAngularVelocity)(uint64_t entityID, float velocity);
		int   (*Rigidbody2D_GetBodyType)(uint64_t entityID);
		void  (*Rigidbody2D_SetBodyType)(uint64_t entityID, int type);
		float (*Rigidbody2D_GetGravityScale)(uint64_t entityID);
		void  (*Rigidbody2D_SetGravityScale)(uint64_t entityID, float scale);
		float (*Rigidbody2D_GetMass)(uint64_t entityID);
		void  (*Rigidbody2D_SetMass)(uint64_t entityID, float mass);

		// ── BoxCollider2D ────────────────────────────────────────────
		void  (*BoxCollider2D_GetScale)(uint64_t entityID, float* outX, float* outY);
		void  (*BoxCollider2D_GetCenter)(uint64_t entityID, float* outX, float* outY);
		void  (*BoxCollider2D_SetEnabled)(uint64_t entityID, int enabled);

		// ── AudioSource ──────────────────────────────────────────────
		void  (*AudioSource_Play)(uint64_t entityID);
		void  (*AudioSource_Pause)(uint64_t entityID);
		void  (*AudioSource_Stop)(uint64_t entityID);
		void  (*AudioSource_Resume)(uint64_t entityID);
		float (*AudioSource_GetVolume)(uint64_t entityID);
		void  (*AudioSource_SetVolume)(uint64_t entityID, float volume);
		float (*AudioSource_GetPitch)(uint64_t entityID);
		void  (*AudioSource_SetPitch)(uint64_t entityID, float pitch);
		int   (*AudioSource_GetLoop)(uint64_t entityID);
		void  (*AudioSource_SetLoop)(uint64_t entityID, int loop);
		int   (*AudioSource_IsPlaying)(uint64_t entityID);
		int   (*AudioSource_IsPaused)(uint64_t entityID);

		// ── Bolt-Physics ─────────────────────────────────────────────
		int   (*BoltBody2D_GetBodyType)(uint64_t entityID);
		void  (*BoltBody2D_SetBodyType)(uint64_t entityID, int type);
		float (*BoltBody2D_GetMass)(uint64_t entityID);
		void  (*BoltBody2D_SetMass)(uint64_t entityID, float mass);
		int   (*BoltBody2D_GetUseGravity)(uint64_t entityID);
		void  (*BoltBody2D_SetUseGravity)(uint64_t entityID, int enabled);
		void  (*BoltBody2D_GetVelocity)(uint64_t entityID, float* outX, float* outY);
		void  (*BoltBody2D_SetVelocity)(uint64_t entityID, float x, float y);
		void  (*BoltBoxCollider2D_GetHalfExtents)(uint64_t entityID, float* outX, float* outY);
		void  (*BoltBoxCollider2D_SetHalfExtents)(uint64_t entityID, float x, float y);
		float (*BoltCircleCollider2D_GetRadius)(uint64_t entityID);
		void  (*BoltCircleCollider2D_SetRadius)(uint64_t entityID, float radius);

		// ── Scene ────────────────────────────────────────────────────
		const char* (*Scene_GetActiveSceneName)();
		int         (*Scene_GetEntityCount)();
		int         (*Scene_GetEntityCountByName)(const char* sceneName);
		int         (*Scene_LoadAdditive)(const char* sceneName);
		int         (*Scene_Load)(const char* sceneName);
		void        (*Scene_Unload)(const char* sceneName);
		int         (*Scene_SetActive)(const char* sceneName);
		int         (*Scene_Reload)(const char* sceneName);
		int         (*Scene_GetLoadedCount)();
		const char* (*Scene_GetLoadedSceneNameAt)(int index);
		const char* (*Scene_GetEntityNameByUUID)(uint64_t uuid);
		int         (*Scene_QueryEntities)(const char* componentNames, uint64_t* outEntityIDs, int maxOut);
		int         (*Scene_QueryEntitiesFiltered)(const char* withComponents, const char* withoutComponents, const char* mustHaveComponents, int enableFilter, uint64_t* outEntityIDs, int maxOut);
		int         (*Scene_QueryEntitiesInScene)(const char* sceneName, const char* componentNames, uint64_t* outEntityIDs, int maxOut);
		int         (*Scene_QueryEntitiesFilteredInScene)(const char* sceneName, const char* withComponents, const char* withoutComponents, const char* mustHaveComponents, int enableFilter, uint64_t* outEntityIDs, int maxOut);
		int         (*Asset_IsValid)(uint64_t assetId);
		uint64_t    (*Asset_GetOrCreateUUIDFromPath)(const char* path);
		const char* (*Asset_GetPath)(uint64_t assetId);
		const char* (*Asset_GetDisplayName)(uint64_t assetId);
		int         (*Texture_LoadAsset)(uint64_t assetId);
		int         (*Texture_GetWidth)(uint64_t assetId);
		int         (*Texture_GetHeight)(uint64_t assetId);
		int         (*Audio_LoadAsset)(uint64_t assetId);
		void        (*Audio_PlayOneShotAsset)(uint64_t assetId, float volume);

		// ── ParticleSystem2D ─────────────────────────────────────────
		void  (*ParticleSystem2D_Play)(uint64_t entityID);
		void  (*ParticleSystem2D_Pause)(uint64_t entityID);
		void  (*ParticleSystem2D_Stop)(uint64_t entityID);
		int   (*ParticleSystem2D_IsPlaying)(uint64_t entityID);
		int   (*ParticleSystem2D_GetPlayOnAwake)(uint64_t entityID);
		void  (*ParticleSystem2D_SetPlayOnAwake)(uint64_t entityID, int enabled);
		void  (*ParticleSystem2D_GetColor)(uint64_t entityID, float* r, float* g, float* b, float* a);
		void  (*ParticleSystem2D_SetColor)(uint64_t entityID, float r, float g, float b, float a);
		float (*ParticleSystem2D_GetLifeTime)(uint64_t entityID);
		void  (*ParticleSystem2D_SetLifeTime)(uint64_t entityID, float lifetime);
		float (*ParticleSystem2D_GetSpeed)(uint64_t entityID);
		void  (*ParticleSystem2D_SetSpeed)(uint64_t entityID, float speed);
		float (*ParticleSystem2D_GetScale)(uint64_t entityID);
		void  (*ParticleSystem2D_SetScale)(uint64_t entityID, float scale);
		int   (*ParticleSystem2D_GetEmitOverTime)(uint64_t entityID);
		void  (*ParticleSystem2D_SetEmitOverTime)(uint64_t entityID, int rate);
		void  (*ParticleSystem2D_Emit)(uint64_t entityID, int count);

		// ── Gizmos ───────────────────────────────────────────────────
		void (*Gizmo_DrawLine)(float x1, float y1, float x2, float y2);
		void (*Gizmo_DrawSquare)(float cx, float cy, float sx, float sy, float degrees);
		void (*Gizmo_DrawCircle)(float cx, float cy, float radius, int segments);
		void (*Gizmo_SetColor)(float r, float g, float b, float a);
		void (*Gizmo_GetColor)(float* r, float* g, float* b, float* a);
		float (*Gizmo_GetLineWidth)();
		void (*Gizmo_SetLineWidth)(float width);

		// ── Physics2D ────────────────────────────────────────────────
		int (*Physics2D_Raycast)(float originX, float originY, float dirX, float dirY, float distance,
		                         uint64_t* hitEntityID, float* hitX, float* hitY, float* hitNormalX, float* hitNormalY, float* hitDistance);
		int (*Physics2D_OverlapCircle)(float originX, float originY, float radius, int mode, uint64_t* entityID);
		int (*Physics2D_OverlapBox)(float originX, float originY, float halfX, float halfY, float degrees, int mode, uint64_t* entityID);
		int (*Physics2D_OverlapPolygon)(float originX, float originY, const float* points, int pointCount, int mode, uint64_t* entityID);
		int (*Physics2D_OverlapCircleAll)(float originX, float originY, float radius, uint64_t* outEntityIDs, int maxOut);
		int (*Physics2D_OverlapBoxAll)(float originX, float originY, float halfX, float halfY, float degrees, uint64_t* outEntityIDs, int maxOut);
		int (*Physics2D_OverlapPolygonAll)(float originX, float originY, const float* points, int pointCount, uint64_t* outEntityIDs, int maxOut);
	};

	/// Layout must match C# ManagedCallbacksStruct exactly.
	struct ManagedCallbacks
	{
		int32_t (*CreateScriptInstance)(const char* className, uint64_t entityID);
		void    (*DestroyScriptInstance)(int32_t handle);
		void    (*InvokeStart)(int32_t handle);
		void    (*InvokeUpdate)(int32_t handle);
		void    (*InvokeOnDestroy)(int32_t handle);
		int     (*ClassExists)(const char* className);
		int     (*LoadUserAssembly)(const char* path);
		void    (*UnloadUserAssembly)();
		const char* (*GetScriptFields)(int32_t handle);
		void    (*SetScriptField)(int32_t handle, const char* fieldName, const char* value);
		const char* (*GetClassFieldDefs)(const char* className);
		void    (*RaiseApplicationStart)();
		void    (*RaiseApplicationPaused)();
		void    (*RaiseFocusChanged)(int focused);
		void    (*RaiseKeyDown)(int key);
		void    (*RaiseKeyUp)(int key);
		void    (*RaiseMouseDown)(int button);
		void    (*RaiseMouseUp)(int button);
		void    (*RaiseMouseScroll)(float delta);
		void    (*RaiseMouseMove)(float x, float y);
		void    (*RaiseBeforeSceneLoaded)(const char* sceneName);
		void    (*RaiseSceneLoaded)(const char* sceneName);
		void    (*RaiseBeforeSceneUnloaded)(const char* sceneName);
		void    (*RaiseSceneUnloaded)(const char* sceneName);
	};

} // namespace Bolt

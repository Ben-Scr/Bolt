#include "pch.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/SceneSerializerShared.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Scene/Scene.hpp"
#include "Scene/EntityHelper.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/General/RectTransformComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Components/Tags.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Components/Physics/BoltBody2DComponent.hpp"
#include "Components/Physics/BoltBoxCollider2DComponent.hpp"
#include "Components/Physics/BoltCircleCollider2DComponent.hpp"
#include "Graphics/TextureManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Physics/PhysicsTypes.hpp"
#include "Core/Log.hpp"

#include <cmath>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <vector>

namespace Bolt {

	using Json::Value;
	using namespace SceneSerializerShared;

	namespace {
		static constexpr int SCENE_FORMAT_VERSION = 1;
		static constexpr float k_MinScaleAxis = 0.0001f;

		Value SerializeScriptFields(const ScriptComponent& scriptComponent) {
			Value fieldsByClass = Value::MakeObject();
			auto& callbacks = ScriptEngine::GetCallbacks();

			for (const ScriptInstance& instance : scriptComponent.Scripts) {
				if (instance.GetType() == ScriptType::Native) {
					continue;
				}

				const char* rawJson = nullptr;
				const bool hasLiveInstance = instance.HasManagedInstance();

				if (hasLiveInstance && callbacks.GetScriptFields) {
					rawJson = callbacks.GetScriptFields(static_cast<int32_t>(instance.GetGCHandle()));
				}
				else if (callbacks.GetClassFieldDefs) {
					rawJson = callbacks.GetClassFieldDefs(instance.GetClassName().c_str());
				}

				if (!rawJson || !*rawJson) {
					continue;
				}

				Value fieldArray;
				std::string parseError;
				if (!Json::TryParse(rawJson, fieldArray, &parseError) || !fieldArray.IsArray()) {
					BT_CORE_WARN_TAG(
						"SceneSerializer",
						"Failed to parse script field metadata for {}: {}",
						instance.GetClassName(),
						parseError);
					continue;
				}

				if (!hasLiveInstance && !scriptComponent.PendingFieldValues.empty()) {
					const std::string prefix = instance.GetClassName() + ".";
					for (Value& fieldValue : fieldArray.GetArray()) {
						if (!fieldValue.IsObject()) {
							continue;
						}

						const std::string fieldName = GetStringMember(fieldValue, "name");
						if (fieldName.empty()) {
							continue;
						}

						const auto pendingValueIt = scriptComponent.PendingFieldValues.find(prefix + fieldName);
						if (pendingValueIt == scriptComponent.PendingFieldValues.end()) {
							continue;
						}

						fieldValue.AddMember("value", Value(pendingValueIt->second));
					}
				}

				for (Value& fieldValue : fieldArray.GetArray()) {
					if (!fieldValue.IsObject()) {
						continue;
					}

					const std::string fieldType = GetStringMember(fieldValue, "type");
					const std::string currentValue = GetStringMember(fieldValue, "value");
					const std::string normalizedValue = NormalizeScriptAssetValue(fieldType, currentValue);
					if (normalizedValue != currentValue) {
						fieldValue.AddMember("value", Value(normalizedValue));
					}
				}

				if (!fieldArray.GetArray().empty()) {
					fieldsByClass.AddMember(instance.GetClassName(), std::move(fieldArray));
				}
			}

			return fieldsByClass;
		}

		Value SerializeEntity(Scene& scene, EntityHandle entity) {
			auto& registry = scene.GetRegistry();
			Value entityValue = Value::MakeObject();

			std::string name = "Entity";
			if (registry.all_of<NameComponent>(entity)) {
				name = registry.get<NameComponent>(entity).Name;
			}
			entityValue.AddMember("name", Value(name));

			if (registry.all_of<UUIDComponent>(entity)) {
				entityValue.AddMember(
					"uuid",
					Value(std::to_string(static_cast<uint64_t>(registry.get<UUIDComponent>(entity).Id))));
			}

			if (registry.all_of<Transform2DComponent>(entity)) {
				const auto& transform = registry.get<Transform2DComponent>(entity);
				Value transformValue = Value::MakeObject();
				transformValue.AddMember("posX", Value(transform.Position.x));
				transformValue.AddMember("posY", Value(transform.Position.y));
				transformValue.AddMember("rotation", Value(transform.Rotation));
				transformValue.AddMember("scaleX", Value(transform.Scale.x));
				transformValue.AddMember("scaleY", Value(transform.Scale.y));
				entityValue.AddMember("Transform2D", std::move(transformValue));
			}

			if (registry.all_of<SpriteRendererComponent>(entity)) {
				auto& spriteRenderer = registry.get<SpriteRendererComponent>(entity);
				Value spriteValue = Value::MakeObject();
				spriteValue.AddMember("r", Value(spriteRenderer.Color.r));
				spriteValue.AddMember("g", Value(spriteRenderer.Color.g));
				spriteValue.AddMember("b", Value(spriteRenderer.Color.b));
				spriteValue.AddMember("a", Value(spriteRenderer.Color.a));
				spriteValue.AddMember("sortOrder", Value(static_cast<int>(spriteRenderer.SortingOrder)));
				spriteValue.AddMember("sortLayer", Value(static_cast<int>(spriteRenderer.SortingLayer)));

				uint64_t textureAssetId = static_cast<uint64_t>(spriteRenderer.TextureAssetId);
				if (textureAssetId == 0) {
					textureAssetId = TextureManager::GetTextureAssetUUID(spriteRenderer.TextureHandle);
					if (textureAssetId != 0) {
						spriteRenderer.TextureAssetId = UUID(textureAssetId);
					}
				}

				const std::string textureName = TextureManager::GetTextureName(spriteRenderer.TextureHandle);
				if (!textureName.empty()) {
					spriteValue.AddMember("texture", Value(textureName));
					Texture2D* texture = TextureManager::GetTexture(spriteRenderer.TextureHandle);
					if (texture) {
						spriteValue.AddMember("filter", Value(static_cast<int>(texture->GetFilter())));
						spriteValue.AddMember("wrapU", Value(static_cast<int>(texture->GetWrapU())));
						spriteValue.AddMember("wrapV", Value(static_cast<int>(texture->GetWrapV())));
					}
				}
				if (textureAssetId != 0) {
					spriteValue.AddMember("textureAsset", Value(std::to_string(textureAssetId)));
				}

				entityValue.AddMember("SpriteRenderer", std::move(spriteValue));
			}

			if (registry.all_of<Rigidbody2DComponent>(entity)) {
				auto& rigidbody = registry.get<Rigidbody2DComponent>(entity);
				Value rigidbodyValue = Value::MakeObject();
				rigidbodyValue.AddMember("bodyType", Value(static_cast<int>(rigidbody.GetBodyType())));
				rigidbodyValue.AddMember("gravityScale", Value(rigidbody.GetGravityScale()));
				rigidbodyValue.AddMember("mass", Value(rigidbody.GetMass()));
				entityValue.AddMember("Rigidbody2D", std::move(rigidbodyValue));
			}

			if (registry.all_of<BoxCollider2DComponent>(entity)) {
				auto& collider = registry.get<BoxCollider2DComponent>(entity);
				const Vec2 scale = collider.GetScale();
				const Vec2 center = collider.GetCenter();
				Value colliderValue = Value::MakeObject();
				colliderValue.AddMember("scaleX", Value(scale.x));
				colliderValue.AddMember("scaleY", Value(scale.y));
				colliderValue.AddMember("centerX", Value(center.x));
				colliderValue.AddMember("centerY", Value(center.y));
				colliderValue.AddMember("friction", Value(collider.GetFriction()));
				colliderValue.AddMember("bounciness", Value(collider.GetBounciness()));
				colliderValue.AddMember("layer", Value(collider.GetLayer()));
				colliderValue.AddMember("registerContacts", Value(collider.CanRegisterContacts()));
				colliderValue.AddMember("sensor", Value(collider.IsSensor()));
				entityValue.AddMember("BoxCollider2D", std::move(colliderValue));
			}

			if (registry.all_of<AudioSourceComponent>(entity)) {
				auto& audioSource = registry.get<AudioSourceComponent>(entity);
				Value audioValue = Value::MakeObject();
				audioValue.AddMember("volume", Value(audioSource.GetVolume()));
				audioValue.AddMember("pitch", Value(audioSource.GetPitch()));
				audioValue.AddMember("loop", Value(audioSource.IsLooping()));
				audioValue.AddMember("playOnAwake", Value(audioSource.GetPlayOnAwake()));

				uint64_t audioAssetId = static_cast<uint64_t>(audioSource.GetAudioAssetId());
				if (audioAssetId == 0) {
					audioAssetId = AudioManager::GetAudioAssetUUID(audioSource.GetAudioHandle());
					if (audioAssetId != 0) {
						audioSource.SetAudioAssetId(UUID(audioAssetId));
					}
				}

				const std::string audioPath = AudioManager::GetAudioName(audioSource.GetAudioHandle());
				if (!audioPath.empty()) {
					audioValue.AddMember("clip", Value(audioPath));
				}
				if (audioAssetId != 0) {
					audioValue.AddMember("clipAsset", Value(std::to_string(audioAssetId)));
				}

				entityValue.AddMember("AudioSource", std::move(audioValue));
			}

			if (registry.all_of<StaticTag>(entity)) {
				entityValue.AddMember("static", Value(true));
			}
			if (registry.all_of<DisabledTag>(entity)) {
				entityValue.AddMember("disabled", Value(true));
			}
			if (registry.all_of<DeadlyTag>(entity)) {
				entityValue.AddMember("deadly", Value(true));
			}

			if (registry.all_of<Camera2DComponent>(entity)) {
				auto& camera = registry.get<Camera2DComponent>(entity);
				const Color clearColor = camera.GetClearColor();
				Value cameraValue = Value::MakeObject();
				cameraValue.AddMember("orthoSize", Value(camera.GetOrthographicSize()));
				cameraValue.AddMember("zoom", Value(camera.GetZoom()));
				cameraValue.AddMember("clearR", Value(clearColor.r));
				cameraValue.AddMember("clearG", Value(clearColor.g));
				cameraValue.AddMember("clearB", Value(clearColor.b));
				cameraValue.AddMember("clearA", Value(clearColor.a));
				entityValue.AddMember("Camera2D", std::move(cameraValue));
			}

			if (registry.all_of<BoltBody2DComponent>(entity)) {
				const auto& boltBody = registry.get<BoltBody2DComponent>(entity);
				Value bodyValue = Value::MakeObject();
				bodyValue.AddMember("type", Value(static_cast<int>(boltBody.Type)));
				bodyValue.AddMember("mass", Value(boltBody.Mass));
				bodyValue.AddMember("useGravity", Value(boltBody.UseGravity));
				bodyValue.AddMember("boundaryCheck", Value(boltBody.BoundaryCheck));
				entityValue.AddMember("BoltBody2D", std::move(bodyValue));
			}

			if (registry.all_of<BoltBoxCollider2DComponent>(entity)) {
				const auto& collider = registry.get<BoltBoxCollider2DComponent>(entity);
				Value colliderValue = Value::MakeObject();
				colliderValue.AddMember("halfX", Value(collider.HalfExtents.x));
				colliderValue.AddMember("halfY", Value(collider.HalfExtents.y));
				entityValue.AddMember("BoltBoxCollider2D", std::move(colliderValue));
			}

			if (registry.all_of<BoltCircleCollider2DComponent>(entity)) {
				const auto& collider = registry.get<BoltCircleCollider2DComponent>(entity);
				Value colliderValue = Value::MakeObject();
				colliderValue.AddMember("radius", Value(collider.Radius));
				entityValue.AddMember("BoltCircleCollider2D", std::move(colliderValue));
			}

			if (registry.all_of<ParticleSystem2DComponent>(entity)) {
				auto& particleSystem = registry.get<ParticleSystem2DComponent>(entity);
				Value particleValue = Value::MakeObject();
				const int shapeType =
					std::holds_alternative<ParticleSystem2DComponent::CircleParams>(particleSystem.Shape) ? 0 : 1;

				particleValue.AddMember("playOnAwake", Value(particleSystem.PlayOnAwake));
				particleValue.AddMember("lifetime", Value(particleSystem.ParticleSettings.LifeTime));
				particleValue.AddMember("speed", Value(particleSystem.ParticleSettings.Speed));
				particleValue.AddMember("scale", Value(particleSystem.ParticleSettings.Scale));
				particleValue.AddMember("gravityX", Value(particleSystem.ParticleSettings.Gravity.x));
				particleValue.AddMember("gravityY", Value(particleSystem.ParticleSettings.Gravity.y));
				particleValue.AddMember("useGravity", Value(particleSystem.ParticleSettings.UseGravity));
				particleValue.AddMember("useRandomColors", Value(particleSystem.ParticleSettings.UseRandomColors));
				particleValue.AddMember("moveDirectionX", Value(particleSystem.ParticleSettings.MoveDirection.x));
				particleValue.AddMember("moveDirectionY", Value(particleSystem.ParticleSettings.MoveDirection.y));
				particleValue.AddMember("emitOverTime", Value(static_cast<int>(particleSystem.EmissionSettings.EmitOverTime)));
				particleValue.AddMember(
					"rateOverDistance",
					Value(static_cast<int>(particleSystem.EmissionSettings.RateOverDistance)));
				particleValue.AddMember(
					"emissionSpace",
					Value(static_cast<int>(particleSystem.EmissionSettings.EmissionSpace)));
				particleValue.AddMember("shapeType", Value(shapeType));

				if (shapeType == 0) {
					const auto& circle = std::get<ParticleSystem2DComponent::CircleParams>(particleSystem.Shape);
					particleValue.AddMember("radius", Value(circle.Radius));
					particleValue.AddMember("isOnCircle", Value(circle.IsOnCircle));
				}
				else {
					const auto& square = std::get<ParticleSystem2DComponent::SquareParams>(particleSystem.Shape);
					particleValue.AddMember("halfExtendsX", Value(square.HalfExtends.x));
					particleValue.AddMember("halfExtendsY", Value(square.HalfExtends.y));
				}

				particleValue.AddMember("maxParticles", Value(static_cast<int64_t>(particleSystem.RenderingSettings.MaxParticles)));
				particleValue.AddMember("colorR", Value(particleSystem.RenderingSettings.Color.r));
				particleValue.AddMember("colorG", Value(particleSystem.RenderingSettings.Color.g));
				particleValue.AddMember("colorB", Value(particleSystem.RenderingSettings.Color.b));
				particleValue.AddMember("colorA", Value(particleSystem.RenderingSettings.Color.a));
				particleValue.AddMember("sortOrder", Value(static_cast<int>(particleSystem.RenderingSettings.SortingOrder)));
				particleValue.AddMember("sortLayer", Value(static_cast<int>(particleSystem.RenderingSettings.SortingLayer)));

				uint64_t textureAssetId = static_cast<uint64_t>(particleSystem.GetTextureAssetId());
				if (textureAssetId == 0) {
					textureAssetId = TextureManager::GetTextureAssetUUID(particleSystem.GetTextureHandle());
					if (textureAssetId != 0) {
						particleSystem.SetTexture(particleSystem.GetTextureHandle(), UUID(textureAssetId));
					}
				}

				const std::string textureName = TextureManager::GetTextureName(particleSystem.GetTextureHandle());
				if (!textureName.empty()) {
					particleValue.AddMember("texture", Value(textureName));
				}
				if (textureAssetId != 0) {
					particleValue.AddMember("textureAsset", Value(std::to_string(textureAssetId)));
				}

				entityValue.AddMember("ParticleSystem2D", std::move(particleValue));
			}

			if (registry.all_of<RectTransformComponent>(entity)) {
				const auto& rectTransform = registry.get<RectTransformComponent>(entity);
				Value rectValue = Value::MakeObject();
				rectValue.AddMember("posX", Value(rectTransform.Position.x));
				rectValue.AddMember("posY", Value(rectTransform.Position.y));
				rectValue.AddMember("pivotX", Value(rectTransform.Pivot.x));
				rectValue.AddMember("pivotY", Value(rectTransform.Pivot.y));
				rectValue.AddMember("width", Value(rectTransform.Width));
				rectValue.AddMember("height", Value(rectTransform.Height));
				rectValue.AddMember("rotation", Value(rectTransform.Rotation));
				rectValue.AddMember("scaleX", Value(rectTransform.Scale.x));
				rectValue.AddMember("scaleY", Value(rectTransform.Scale.y));
				entityValue.AddMember("RectTransform", std::move(rectValue));
			}

			if (registry.all_of<ImageComponent>(entity)) {
				auto& image = registry.get<ImageComponent>(entity);
				Value imageValue = Value::MakeObject();
				imageValue.AddMember("r", Value(image.Color.r));
				imageValue.AddMember("g", Value(image.Color.g));
				imageValue.AddMember("b", Value(image.Color.b));
				imageValue.AddMember("a", Value(image.Color.a));

				uint64_t textureAssetId = static_cast<uint64_t>(image.TextureAssetId);
				if (textureAssetId == 0) {
					textureAssetId = TextureManager::GetTextureAssetUUID(image.TextureHandle);
					if (textureAssetId != 0) {
						image.TextureAssetId = UUID(textureAssetId);
					}
				}

				const std::string textureName = TextureManager::GetTextureName(image.TextureHandle);
				if (!textureName.empty()) {
					imageValue.AddMember("texture", Value(textureName));
				}
				if (textureAssetId != 0) {
					imageValue.AddMember("textureAsset", Value(std::to_string(textureAssetId)));
				}

				entityValue.AddMember("Image", std::move(imageValue));
			}

			if (registry.all_of<ScriptComponent>(entity)) {
				const auto& scriptComponent = registry.get<ScriptComponent>(entity);
				if (!scriptComponent.Scripts.empty()) {
					Value scriptsValue = Value::MakeArray();
					for (const ScriptInstance& instance : scriptComponent.Scripts) {
						Value scriptValue = Value::MakeObject();
						scriptValue.AddMember("className", Value(instance.GetClassName()));
						scriptValue.AddMember("type", Value(ScriptTypeToString(instance.GetType())));
						scriptsValue.Append(std::move(scriptValue));
					}
					entityValue.AddMember("Scripts", std::move(scriptsValue));

					Value scriptFields = SerializeScriptFields(scriptComponent);
					if (!scriptFields.GetObject().empty()) {
						entityValue.AddMember("ScriptFields", std::move(scriptFields));
					}
				}
			}

			return entityValue;
		}
	} // namespace

	bool SceneSerializer::LoadFromFile(Scene& scene, const std::string& path) {
		try {
			if (!File::Exists(path)) {
				BT_CORE_WARN_TAG("SceneSerializer", "Scene file not found: {}", path);
				return false;
			}

			const std::string json = File::ReadAllText(path);
			if (json.empty()) {
				BT_CORE_WARN_TAG("SceneSerializer", "Scene file is empty: {}", path);
				return false;
			}

			Value root;
			std::string parseError;
			if (!Json::TryParse(json, root, &parseError) || !root.IsObject()) {
				BT_CORE_ERROR_TAG("SceneSerializer", "Failed to parse scene JSON {}: {}", path, parseError);
				return false;
			}

			return DeserializeScene(scene, root, path);
		}
		catch (const std::exception& exception) {
			BT_CORE_ERROR_TAG("SceneSerializer", "Load failed: {}", exception.what());
			return false;
		}
	}

	bool SceneSerializer::DeserializeScene(Scene& scene, const Json::Value& root, std::string_view source) {
		if (!root.IsObject()) {
			BT_CORE_ERROR_TAG("SceneSerializer", "DeserializeScene requires an object root");
			return false;
		}

		const int version = GetIntMember(root, "version", 1);
		if (version > SCENE_FORMAT_VERSION) {
			BT_CORE_WARN_TAG(
				"SceneSerializer",
				"Scene version {} is newer than supported ({})",
				version,
				SCENE_FORMAT_VERSION);
		}

		const std::string sourcePath(source);
		const std::string serializedName = GetStringMember(root, "name");
		if (!sourcePath.empty()) {
			scene.SetName(std::filesystem::path(sourcePath).stem().string());
		}
		else if (!serializedName.empty()) {
			scene.SetName(serializedName);
		}

		const uint64_t sceneId = GetUInt64Member(root, "sceneId", 0);
		if (sceneId != 0) {
			scene.SetSceneId(UUID(sceneId));
		}

		scene.ClearEntities();

		if (const Value* entitiesValue = GetArrayMember(root, "entities")) {
			for (const Value& entityValue : entitiesValue->GetArray()) {
				if (!entityValue.IsObject()) {
					continue;
				}
				DeserializeEntity(scene, entityValue);
			}
		}
		else {
			if (!sourcePath.empty()) {
				BT_CORE_WARN_TAG("SceneSerializer", "No entities array in scene file: {}", sourcePath);
			}
			else {
				BT_CORE_WARN_TAG("SceneSerializer", "No entities array in scene data");
			}
		}

		if (scene.GetRegistry().view<Camera2DComponent>().size() == 0) {
			EntityHelper::CreateCamera2DEntity(scene);
			BT_CORE_INFO_TAG("SceneSerializer", "Added default camera (none in scene data)");
		}

		scene.ClearDirty();
		BT_CORE_INFO_TAG("SceneSerializer", "Loaded scene: {}", scene.GetName());
		return true;
	}

	EntityHandle SceneSerializer::DeserializeEntity(Scene& scene, const Json::Value& entityValue) {
		if (!entityValue.IsObject()) {
			return entt::null;
		}

		const std::string name = GetStringMember(entityValue, "name", "Entity");
		const EntityHandle entity = scene.CreateEntity(name).GetHandle();

		const uint64_t savedUuid = GetUInt64Member(entityValue, "uuid", 0);
		if (savedUuid != 0 && scene.HasComponent<UUIDComponent>(entity)) {
			scene.GetComponent<UUIDComponent>(entity).Id = UUID(savedUuid);
		}

		if (const Value* transformValue = GetObjectMember(entityValue, "Transform2D")) {
			auto& transform = scene.GetComponent<Transform2DComponent>(entity);
			transform.Position.x = GetFloatMember(*transformValue, "posX", 0.0f);
			transform.Position.y = GetFloatMember(*transformValue, "posY", 0.0f);
			transform.Rotation = GetFloatMember(*transformValue, "rotation", 0.0f);
			transform.Scale.x = GetFloatMember(*transformValue, "scaleX", 1.0f);
			transform.Scale.y = GetFloatMember(*transformValue, "scaleY", 1.0f);
		}

		if (GetBoolMember(entityValue, "static", false)) {
			scene.AddComponent<StaticTag>(entity);
		}
		if (GetBoolMember(entityValue, "disabled", false)) {
			scene.AddComponent<DisabledTag>(entity);
		}
		if (GetBoolMember(entityValue, "deadly", false)) {
			scene.AddComponent<DeadlyTag>(entity);
		}

		if (const Value* spriteValue = GetObjectMember(entityValue, "SpriteRenderer")) {
			auto& spriteRenderer = scene.AddComponent<SpriteRendererComponent>(entity);
			spriteRenderer.Color.r = GetFloatMember(*spriteValue, "r", 1.0f);
			spriteRenderer.Color.g = GetFloatMember(*spriteValue, "g", 1.0f);
			spriteRenderer.Color.b = GetFloatMember(*spriteValue, "b", 1.0f);
			spriteRenderer.Color.a = GetFloatMember(*spriteValue, "a", 1.0f);
			spriteRenderer.SortingOrder = static_cast<short>(GetIntMember(*spriteValue, "sortOrder", 0));
			spriteRenderer.SortingLayer = static_cast<uint8_t>(GetIntMember(*spriteValue, "sortLayer", 0));
			const Filter filter = static_cast<Filter>(GetIntMember(*spriteValue, "filter", static_cast<int>(Filter::Point)));
			const Wrap wrapU = static_cast<Wrap>(GetIntMember(*spriteValue, "wrapU", static_cast<int>(Wrap::Clamp)));
			const Wrap wrapV = static_cast<Wrap>(GetIntMember(*spriteValue, "wrapV", static_cast<int>(Wrap::Clamp)));
			spriteRenderer.TextureHandle = LoadTextureFromValue(
				*spriteValue,
				"textureAsset",
				"texture",
				filter,
				wrapU,
				wrapV,
				&spriteRenderer.TextureAssetId);
		}

		if (const Value* rigidbodyValue = GetObjectMember(entityValue, "Rigidbody2D")) {
			auto& rigidbody = scene.AddComponent<Rigidbody2DComponent>(entity);
			rigidbody.SetBodyType(static_cast<BodyType>(GetIntMember(*rigidbodyValue, "bodyType", static_cast<int>(BodyType::Dynamic))));
			rigidbody.SetGravityScale(GetFloatMember(*rigidbodyValue, "gravityScale", 1.0f));
			rigidbody.SetMass(GetFloatMember(*rigidbodyValue, "mass", 1.0f));
		}

		if (const Value* colliderValue = GetObjectMember(entityValue, "BoxCollider2D")) {
			auto& boxCollider = scene.AddComponent<BoxCollider2DComponent>(entity);
			const Vec2 savedCenter{
				GetFloatMember(*colliderValue, "centerX", 0.0f),
				GetFloatMember(*colliderValue, "centerY", 0.0f)
			};
			boxCollider.SetCenter(savedCenter, scene);

			const auto& transform = scene.GetComponent<Transform2DComponent>(entity);
			const Vec2 savedScale{
				GetFloatMember(*colliderValue, "scaleX", transform.Scale.x),
				GetFloatMember(*colliderValue, "scaleY", transform.Scale.y)
			};
			Vec2 localScale{ 1.0f, 1.0f };
			if (std::fabs(transform.Scale.x) > k_MinScaleAxis) {
				localScale.x = savedScale.x / transform.Scale.x;
			}
			if (std::fabs(transform.Scale.y) > k_MinScaleAxis) {
				localScale.y = savedScale.y / transform.Scale.y;
			}
			boxCollider.SetScale(localScale, scene);
			boxCollider.SetSensor(GetBoolMember(*colliderValue, "sensor", false), scene);
			boxCollider.SetFriction(GetFloatMember(*colliderValue, "friction", boxCollider.GetFriction()));
			boxCollider.SetBounciness(GetFloatMember(*colliderValue, "bounciness", boxCollider.GetBounciness()));
			boxCollider.SetLayer(GetUInt64Member(*colliderValue, "layer", boxCollider.GetLayer()));
			boxCollider.SetRegisterContacts(GetBoolMember(*colliderValue, "registerContacts", boxCollider.CanRegisterContacts()));
		}

		if (const Value* audioValue = GetObjectMember(entityValue, "AudioSource")) {
			auto& audioSource = scene.AddComponent<AudioSourceComponent>(entity);
			audioSource.SetVolume(GetFloatMember(*audioValue, "volume", 1.0f));
			audioSource.SetPitch(GetFloatMember(*audioValue, "pitch", 1.0f));
			audioSource.SetLoop(GetBoolMember(*audioValue, "loop", false));
			audioSource.SetPlayOnAwake(GetBoolMember(*audioValue, "playOnAwake", false));

			UUID audioAssetId = UUID(0);
			const AudioHandle handle = LoadAudioFromValue(*audioValue, "clipAsset", "clip", &audioAssetId);
			if (handle.IsValid()) {
				audioSource.SetAudioHandle(handle, audioAssetId);
			}
		}

		if (const Value* cameraValue = GetObjectMember(entityValue, "Camera2D")) {
			auto& camera = scene.AddComponent<Camera2DComponent>(entity);
			camera.SetOrthographicSize(GetFloatMember(*cameraValue, "orthoSize", 5.0f));
			camera.SetZoom(GetFloatMember(*cameraValue, "zoom", 1.0f));
			camera.SetClearColor(Color(
				GetFloatMember(*cameraValue, "clearR", 0.1f),
				GetFloatMember(*cameraValue, "clearG", 0.1f),
				GetFloatMember(*cameraValue, "clearB", 0.1f),
				GetFloatMember(*cameraValue, "clearA", 1.0f)));
		}

		if (const Value* bodyValue = GetObjectMember(entityValue, "BoltBody2D")) {
			auto& body = scene.AddComponent<BoltBody2DComponent>(entity);
			body.Type = static_cast<BoltPhys::BodyType>(GetIntMember(*bodyValue, "type", 1));
			body.Mass = GetFloatMember(*bodyValue, "mass", 1.0f);
			body.UseGravity = GetBoolMember(*bodyValue, "useGravity", true);
			body.BoundaryCheck = GetBoolMember(*bodyValue, "boundaryCheck", false);

			if (body.m_Body) {
				body.m_Body->SetBodyType(body.Type);
				body.m_Body->SetMass(body.Mass);
				body.m_Body->SetGravityEnabled(body.UseGravity);
				body.m_Body->SetBoundaryCheckEnabled(body.BoundaryCheck);
			}
		}

		if (const Value* colliderValue = GetObjectMember(entityValue, "BoltBoxCollider2D")) {
			auto& collider = scene.AddComponent<BoltBoxCollider2DComponent>(entity);
			collider.HalfExtents = {
				GetFloatMember(*colliderValue, "halfX", 0.5f),
				GetFloatMember(*colliderValue, "halfY", 0.5f)
			};
			if (collider.m_Collider) {
				collider.m_Collider->SetHalfExtents({ collider.HalfExtents.x, collider.HalfExtents.y });
			}
		}

		if (const Value* colliderValue = GetObjectMember(entityValue, "BoltCircleCollider2D")) {
			auto& collider = scene.AddComponent<BoltCircleCollider2DComponent>(entity);
			collider.Radius = GetFloatMember(*colliderValue, "radius", 0.5f);
			if (collider.m_Collider) {
				collider.m_Collider->SetRadius(collider.Radius);
			}
		}

		if (const Value* particleValue = GetObjectMember(entityValue, "ParticleSystem2D")) {
			auto& particleSystem = scene.AddComponent<ParticleSystem2DComponent>(entity);
			particleSystem.PlayOnAwake = GetBoolMember(*particleValue, "playOnAwake", true);
			particleSystem.ParticleSettings.LifeTime = GetFloatMember(*particleValue, "lifetime", 1.0f);
			particleSystem.ParticleSettings.Speed = GetFloatMember(*particleValue, "speed", 5.0f);
			particleSystem.ParticleSettings.Scale = GetFloatMember(*particleValue, "scale", 1.0f);
			particleSystem.ParticleSettings.Gravity.x = GetFloatMember(*particleValue, "gravityX", 0.0f);
			particleSystem.ParticleSettings.Gravity.y = GetFloatMember(*particleValue, "gravityY", 0.0f);
			particleSystem.ParticleSettings.UseGravity = GetBoolMember(*particleValue, "useGravity", false);
			particleSystem.ParticleSettings.UseRandomColors = GetBoolMember(*particleValue, "useRandomColors", false);
			particleSystem.ParticleSettings.MoveDirection.x = GetFloatMember(*particleValue, "moveDirectionX", 0.0f);
			particleSystem.ParticleSettings.MoveDirection.y = GetFloatMember(*particleValue, "moveDirectionY", 0.0f);
			particleSystem.EmissionSettings.EmitOverTime =
				static_cast<uint16_t>(GetIntMember(*particleValue, "emitOverTime", 10));
			particleSystem.EmissionSettings.RateOverDistance =
				static_cast<uint16_t>(GetIntMember(*particleValue, "rateOverDistance", 0));
			particleSystem.EmissionSettings.EmissionSpace = static_cast<ParticleSystem2DComponent::Space>(
				GetIntMember(*particleValue, "emissionSpace", static_cast<int>(ParticleSystem2DComponent::Space::World)));

			if (GetIntMember(*particleValue, "shapeType", 0) == 0) {
				ParticleSystem2DComponent::CircleParams circle;
				circle.Radius = GetFloatMember(*particleValue, "radius", 1.0f);
				circle.IsOnCircle = GetBoolMember(*particleValue, "isOnCircle", false);
				particleSystem.Shape = circle;
			}
			else {
				ParticleSystem2DComponent::SquareParams square;
				square.HalfExtends.x = GetFloatMember(*particleValue, "halfExtendsX", 1.0f);
				square.HalfExtends.y = GetFloatMember(*particleValue, "halfExtendsY", 1.0f);
				particleSystem.Shape = square;
			}

			particleSystem.RenderingSettings.MaxParticles =
				static_cast<uint32_t>(GetUInt64Member(*particleValue, "maxParticles", 1000));
			particleSystem.RenderingSettings.Color.r = GetFloatMember(*particleValue, "colorR", 1.0f);
			particleSystem.RenderingSettings.Color.g = GetFloatMember(*particleValue, "colorG", 1.0f);
			particleSystem.RenderingSettings.Color.b = GetFloatMember(*particleValue, "colorB", 1.0f);
			particleSystem.RenderingSettings.Color.a = GetFloatMember(*particleValue, "colorA", 1.0f);
			particleSystem.RenderingSettings.SortingOrder =
				static_cast<short>(GetIntMember(*particleValue, "sortOrder", 0));
			particleSystem.RenderingSettings.SortingLayer =
				static_cast<uint8_t>(GetIntMember(*particleValue, "sortLayer", 0));

			UUID textureAssetId = UUID(0);
			const TextureHandle textureHandle = LoadTextureFromValue(
				*particleValue,
				"textureAsset",
				"texture",
				Filter::Point,
				Wrap::Clamp,
				Wrap::Clamp,
				&textureAssetId);
			if (textureHandle.IsValid()) {
				particleSystem.SetTexture(textureHandle, textureAssetId);
			}
		}

		if (const Value* rectValue = GetObjectMember(entityValue, "RectTransform")) {
			auto& rectTransform = scene.AddComponent<RectTransformComponent>(entity);
			rectTransform.Position.x = GetFloatMember(*rectValue, "posX", 0.0f);
			rectTransform.Position.y = GetFloatMember(*rectValue, "posY", 0.0f);
			rectTransform.Pivot.x = GetFloatMember(*rectValue, "pivotX", 0.0f);
			rectTransform.Pivot.y = GetFloatMember(*rectValue, "pivotY", 0.0f);
			rectTransform.Width = GetFloatMember(*rectValue, "width", 100.0f);
			rectTransform.Height = GetFloatMember(*rectValue, "height", 100.0f);
			rectTransform.Rotation = GetFloatMember(*rectValue, "rotation", 0.0f);
			rectTransform.Scale.x = GetFloatMember(*rectValue, "scaleX", 1.0f);
			rectTransform.Scale.y = GetFloatMember(*rectValue, "scaleY", 1.0f);
		}

		if (const Value* imageValue = GetObjectMember(entityValue, "Image")) {
			auto& image = scene.AddComponent<ImageComponent>(entity);
			image.Color.r = GetFloatMember(*imageValue, "r", 1.0f);
			image.Color.g = GetFloatMember(*imageValue, "g", 1.0f);
			image.Color.b = GetFloatMember(*imageValue, "b", 1.0f);
			image.Color.a = GetFloatMember(*imageValue, "a", 1.0f);

			image.TextureHandle = LoadTextureFromValue(
				*imageValue,
				"textureAsset",
				"texture",
				Filter::Point,
				Wrap::Clamp,
				Wrap::Clamp,
				&image.TextureAssetId);
		}

		if (const Value* scriptsValue = GetArrayMember(entityValue, "Scripts")) {
			auto& scriptComponent = scene.AddComponent<ScriptComponent>(entity);
			for (const Value& scriptValue : scriptsValue->GetArray()) {
				if (scriptValue.IsString()) {
					scriptComponent.AddScript(scriptValue.AsStringOr(), ScriptType::Unknown);
					continue;
				}

				if (!scriptValue.IsObject()) {
					continue;
				}

				std::string className = GetStringMember(scriptValue, "className");
				if (className.empty()) {
					className = GetStringMember(scriptValue, "class");
				}
				if (className.empty()) {
					continue;
				}

				scriptComponent.AddScript(
					className,
					ScriptTypeFromString(GetStringMember(scriptValue, "type")));
			}

			if (const Value* fieldsByClass = GetObjectMember(entityValue, "ScriptFields")) {
				for (const auto& [className, fieldsValue] : fieldsByClass->GetObject()) {
					if (!scriptComponent.HasScript(className) || !fieldsValue.IsArray()) {
						continue;
					}

					for (const Value& fieldValue : fieldsValue.GetArray()) {
						if (!fieldValue.IsObject()) {
							continue;
						}

						const std::string fieldName = GetStringMember(fieldValue, "name");
						if (fieldName.empty()) {
							continue;
						}

						const Value* valueValue = fieldValue.FindMember("value");
						if (!valueValue) {
							continue;
						}

						scriptComponent.PendingFieldValues[className + "." + fieldName] =
							ValueToFieldString(*valueValue);
					}
				}
			}
		}

		return entity;
	}

	EntityHandle SceneSerializer::LoadEntityFromFile(Scene& scene, const std::string& path) {
		try {
			if (!File::Exists(path)) {
				BT_CORE_WARN_TAG("SceneSerializer", "Prefab file not found: {}", path);
				return entt::null;
			}

			const std::string json = File::ReadAllText(path);
			if (json.empty()) {
				BT_CORE_WARN_TAG("SceneSerializer", "Prefab file is empty: {}", path);
				return entt::null;
			}

			Value root;
			std::string parseError;
			if (!Json::TryParse(json, root, &parseError) || !root.IsObject()) {
				BT_CORE_ERROR_TAG("SceneSerializer", "Failed to parse prefab JSON {}: {}", path, parseError);
				return entt::null;
			}

			const Value* prefabValue = GetObjectMember(root, "prefab");
			if (!prefabValue) {
				BT_CORE_WARN_TAG("SceneSerializer", "No prefab block in file: {}", path);
				return entt::null;
			}

			return DeserializeEntity(scene, *prefabValue);
		}
		catch (const std::exception& exception) {
			BT_CORE_ERROR_TAG("SceneSerializer", "LoadEntityFromFile failed: {}", exception.what());
			return entt::null;
		}
	}

} // namespace Bolt

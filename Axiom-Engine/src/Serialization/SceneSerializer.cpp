#include "pch.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/SceneSerializerShared.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/ComponentInfo.hpp"
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
#include "Components/Physics/FastBody2DComponent.hpp"
#include "Components/Physics/FastBoxCollider2DComponent.hpp"
#include "Components/Physics/FastCircleCollider2DComponent.hpp"
#include "Graphics/TextureManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Physics/PhysicsTypes.hpp"
#include "Core/Log.hpp"

#include <cmath>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <vector>

namespace Axiom {

	using Json::Value;
	using namespace SceneSerializerShared;

	namespace {
		static constexpr int SCENE_FORMAT_VERSION = 1;
		static constexpr float k_MinScaleAxis = 0.0001f;

		const char* EntityOriginToString(EntityOrigin origin) {
			switch (origin) {
			case EntityOrigin::Scene: return "Scene";
			case EntityOrigin::Prefab: return "Prefab";
			case EntityOrigin::Runtime: return "Runtime";
			default: return "Runtime";
			}
		}

		void RemoveObjectMember(Value& value, std::string_view key) {
			if (!value.IsObject()) {
				return;
			}

			auto& members = value.GetObject();
			members.erase(
				std::remove_if(members.begin(), members.end(), [key](const auto& member) {
					return member.first == key;
				}),
				members.end());
		}

		void RemoveEntityIdentityMembers(Value& value) {
			RemoveObjectMember(value, "Origin");
			RemoveObjectMember(value, "RuntimeID");
			RemoveObjectMember(value, "EntityID");
			RemoveObjectMember(value, "SceneGUID");
			RemoveObjectMember(value, "PrefabGUID");
			RemoveObjectMember(value, "uuid");
		}

		bool JsonEquivalent(const Value& left, const Value& right) {
			return Json::Stringify(left, false) == Json::Stringify(right, false);
		}

		void BuildOverridePatch(const Value& prefabValue, const Value& instanceValue, const std::string& prefix, Value& overrides) {
			if (prefabValue.IsObject() && instanceValue.IsObject()) {
				for (const auto& [key, instanceMember] : instanceValue.GetObject()) {
					const Value* prefabMember = prefabValue.FindMember(key);
					const std::string path = prefix.empty() ? key : prefix + "." + key;
					if (prefabMember && prefabMember->IsObject() && instanceMember.IsObject()) {
						BuildOverridePatch(*prefabMember, instanceMember, path, overrides);
						continue;
					}

					if (!prefabMember || !JsonEquivalent(*prefabMember, instanceMember)) {
						overrides.AddMember(path, instanceMember);
					}
				}
				return;
			}

			if (!JsonEquivalent(prefabValue, instanceValue)) {
				overrides.AddMember(prefix, instanceValue);
			}
		}

		bool LoadPrefabEntityValue(uint64_t prefabGuid, Value& outEntityValue) {
			const std::string prefabPath = AssetRegistry::ResolvePath(prefabGuid);
			if (prefabPath.empty() || !File::Exists(prefabPath)) {
				return false;
			}

			Value root;
			std::string parseError;
			if (!Json::TryParse(File::ReadAllText(prefabPath), root, &parseError) || !root.IsObject()) {
				AIM_CORE_WARN_TAG("SceneSerializer", "Failed to parse prefab {}: {}", prefabPath, parseError);
				return false;
			}

			if (const Value* entityValue = GetObjectMember(root, "Entity")) {
				outEntityValue = *entityValue;
				return true;
			}
			if (const Value* entityValue = GetObjectMember(root, "prefab")) {
				outEntityValue = *entityValue;
				return true;
			}

			if (root.FindMember("Transform2D") || root.FindMember("Scripts") || root.FindMember("Name")) {
				outEntityValue = root;
				return true;
			}

			return false;
		}

		Value SerializeEntity(Scene& scene, EntityHandle entity);

		Value SerializePrefabInstance(Scene& scene, EntityHandle entity) {
			Value instanceValue = Value::MakeObject();
			const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entity));
			instanceValue.AddMember("Origin", Value("Prefab"));
			instanceValue.AddMember("PrefabGUID", Value(std::to_string(prefabGuid)));

			Value prefabEntityValue;
			if (prefabGuid == 0 || !LoadPrefabEntityValue(prefabGuid, prefabEntityValue)) {
				return instanceValue;
			}

			Value currentEntityValue = SerializeEntity(scene, entity);
			RemoveEntityIdentityMembers(prefabEntityValue);
			RemoveEntityIdentityMembers(currentEntityValue);

			Value overrides = Value::MakeObject();
			BuildOverridePatch(prefabEntityValue, currentEntityValue, {}, overrides);
			instanceValue.AddMember("Overrides", std::move(overrides));
			return instanceValue;
		}

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
					AIM_CORE_WARN_TAG(
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

			for (const std::string& className : scriptComponent.ManagedComponents) {
				if (className.empty() || !callbacks.GetClassFieldDefs) {
					continue;
				}

				const char* rawJson = callbacks.GetClassFieldDefs(className.c_str());
				if (!rawJson || !*rawJson) {
					continue;
				}

				Value fieldArray;
				std::string parseError;
				if (!Json::TryParse(rawJson, fieldArray, &parseError) || !fieldArray.IsArray()) {
					AIM_CORE_WARN_TAG("SceneSerializer", "Failed to parse managed component field metadata for {}: {}", className, parseError);
					continue;
				}

				const std::string prefix = className + ".";
				for (Value& fieldValue : fieldArray.GetArray()) {
					if (!fieldValue.IsObject()) {
						continue;
					}

					const std::string fieldName = GetStringMember(fieldValue, "name");
					if (fieldName.empty()) {
						continue;
					}

					const auto pendingValueIt = scriptComponent.PendingFieldValues.find(prefix + fieldName);
					if (pendingValueIt != scriptComponent.PendingFieldValues.end()) {
						fieldValue.AddMember("value", Value(pendingValueIt->second));
					}

					const std::string fieldType = GetStringMember(fieldValue, "type");
					const std::string currentValue = GetStringMember(fieldValue, "value");
					const std::string normalizedValue = NormalizeScriptAssetValue(fieldType, currentValue);
					if (normalizedValue != currentValue) {
						fieldValue.AddMember("value", Value(normalizedValue));
					}
				}

				if (!fieldArray.GetArray().empty()) {
					fieldsByClass.AddMember(className, std::move(fieldArray));
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

			if (const EntityMetaData* metaData = scene.GetEntityMetaData(entity)) {
				entityValue.AddMember("Origin", Value(EntityOriginToString(metaData->Origin)));
				if (metaData->Origin == EntityOrigin::Scene && static_cast<uint64_t>(metaData->SceneGUID) != 0) {
					entityValue.AddMember("SceneGUID", Value(std::to_string(static_cast<uint64_t>(metaData->SceneGUID))));
				}
				if (metaData->Origin == EntityOrigin::Prefab && static_cast<uint64_t>(metaData->PrefabGUID) != 0) {
					entityValue.AddMember("PrefabGUID", Value(std::to_string(static_cast<uint64_t>(metaData->PrefabGUID))));
				}
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

				if (textureAssetId != 0) {
					spriteValue.AddMember("textureAsset", Value(std::to_string(textureAssetId)));
					Texture2D* texture = TextureManager::GetTexture(spriteRenderer.TextureHandle);
					if (texture) {
						spriteValue.AddMember("filter", Value(static_cast<int>(texture->GetFilter())));
						spriteValue.AddMember("wrapU", Value(static_cast<int>(texture->GetWrapU())));
						spriteValue.AddMember("wrapV", Value(static_cast<int>(texture->GetWrapV())));
					}
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

			if (registry.all_of<FastBody2DComponent>(entity)) {
				const auto& axiomBody = registry.get<FastBody2DComponent>(entity);
				Value bodyValue = Value::MakeObject();
				bodyValue.AddMember("type", Value(static_cast<int>(axiomBody.Type)));
				bodyValue.AddMember("mass", Value(axiomBody.Mass));
				bodyValue.AddMember("useGravity", Value(axiomBody.UseGravity));
				bodyValue.AddMember("boundaryCheck", Value(axiomBody.BoundaryCheck));
				entityValue.AddMember("FastBody2D", std::move(bodyValue));
			}

			if (registry.all_of<FastBoxCollider2DComponent>(entity)) {
				const auto& collider = registry.get<FastBoxCollider2DComponent>(entity);
				Value colliderValue = Value::MakeObject();
				colliderValue.AddMember("halfX", Value(collider.HalfExtents.x));
				colliderValue.AddMember("halfY", Value(collider.HalfExtents.y));
				entityValue.AddMember("FastBoxCollider2D", std::move(colliderValue));
			}

			if (registry.all_of<FastCircleCollider2DComponent>(entity)) {
				const auto& collider = registry.get<FastCircleCollider2DComponent>(entity);
				Value colliderValue = Value::MakeObject();
				colliderValue.AddMember("radius", Value(collider.Radius));
				entityValue.AddMember("FastCircleCollider2D", std::move(colliderValue));
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
				}
				if (!scriptComponent.ManagedComponents.empty()) {
					Value componentsValue = Value::MakeArray();
					for (const std::string& className : scriptComponent.ManagedComponents) {
						componentsValue.Append(Value(className));
					}
					entityValue.AddMember("ManagedComponents", std::move(componentsValue));
				}

				if (!scriptComponent.Scripts.empty() || !scriptComponent.ManagedComponents.empty()) {
					Value scriptFields = SerializeScriptFields(scriptComponent);
					if (!scriptFields.GetObject().empty()) {
						entityValue.AddMember("ScriptFields", std::move(scriptFields));
					}
				}
			}

			// Registry-driven serialize for package components (mirror of deserialize sweep).
			{
				Entity entityWrapper = scene.GetEntity(entity);
				SceneManager::Get().GetComponentRegistry().ForEachComponentInfo(
					[&](const std::type_index&, const ComponentInfo& info) {
						if (!info.serialize || !info.has || info.serializedName.empty()) return;
						if (!info.has(entityWrapper)) return;
						try {
							Value componentValue = info.serialize(entityWrapper);
							entityValue.AddMember(info.serializedName, std::move(componentValue));
						} catch (const std::exception& e) {
							AIM_CORE_ERROR_TAG("SceneSerializer",
								"Package component '{}' serialize threw: {}",
								info.serializedName, e.what());
						} catch (...) {
							AIM_CORE_ERROR_TAG("SceneSerializer",
								"Package component '{}' serialize threw an unknown exception",
								info.serializedName);
						}
					});
			}

			return entityValue;
		}
	} // namespace

	Json::Value SceneSerializer::SerializeScene(Scene& scene) {
		Value root = Value::MakeObject();
		root.AddMember("version", Value(SCENE_FORMAT_VERSION));
		root.AddMember("name", Value(scene.GetName()));
		root.AddMember("sceneId", Value(std::to_string(static_cast<uint64_t>(scene.GetSceneId()))));

		Value systemsValue = Value::MakeArray();
		for (const std::string& className : scene.GetGameSystemClassNames()) {
			if (!className.empty()) {
				systemsValue.Append(Value(className));
			}
		}
		root.AddMember("systems", std::move(systemsValue));

		Value entitiesValue = Value::MakeArray();
		auto& registry = scene.GetRegistry();
		auto view = registry.view<entt::entity>();
		std::vector<entt::entity> entities(view.begin(), view.end());

		for (const entt::entity entity : entities) {
			const EntityOrigin origin = scene.GetEntityOrigin(entity);
			switch (origin) {
			case EntityOrigin::Scene:
				entitiesValue.Append(SerializeEntity(scene, entity));
				break;
			case EntityOrigin::Prefab:
				entitiesValue.Append(SerializePrefabInstance(scene, entity));
				break;
			case EntityOrigin::Runtime:
				break;
			}
		}

		root.AddMember("entities", std::move(entitiesValue));
		return root;
	}

	bool SceneSerializer::SaveToFile(Scene& scene, const std::string& path) {
		try {
			const std::filesystem::path parentDir = std::filesystem::path(path).parent_path();
			if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
				std::filesystem::create_directories(parentDir);
			}

			File::WriteAllText(path, Json::Stringify(SerializeScene(scene), true));
			AssetRegistry::GetOrCreateAssetUUID(path);
			scene.ClearDirty();
			AIM_CORE_INFO_TAG("SceneSerializer", "Saved scene: {}", scene.GetName());
			return true;
		}
		catch (const std::exception& exception) {
			AIM_CORE_ERROR_TAG("SceneSerializer", "Save failed: {}", exception.what());
			return false;
		}
	}

	Json::Value SceneSerializer::SerializeEntityFull(Scene& scene, EntityHandle entity) {
		if (entity == entt::null || !scene.IsValid(entity)) {
			return Value::MakeObject();
		}

		return SerializeEntity(scene, entity);
	}

	Json::Value SceneSerializer::SerializeEntityForClipboard(Scene& scene, EntityHandle entity) {
		if (entity == entt::null || !scene.IsValid(entity)) {
			return Value::MakeObject();
		}

		Value entityValue = SerializeEntity(scene, entity);
		RemoveEntityIdentityMembers(entityValue);
		return entityValue;
	}

	Json::Value SceneSerializer::SerializeComponent(Scene& scene, EntityHandle entity, std::string_view componentName) {
		if (entity == entt::null || !scene.IsValid(entity) || componentName.empty()) {
			return Value();
		}

		Value entityValue = SerializeEntity(scene, entity);
		if (const Value* componentValue = entityValue.FindMember(componentName)) {
			return *componentValue;
		}

		return Value();
	}

	bool SceneSerializer::SaveEntityToFile(Scene& scene, EntityHandle entity, const std::string& path) {
		try {
			const std::filesystem::path parentDir = std::filesystem::path(path).parent_path();
			if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
				std::filesystem::create_directories(parentDir);
			}

			Value prefabEntity = SerializeEntity(scene, entity);
			RemoveEntityIdentityMembers(prefabEntity);

			// AssetRegistry::GetOrCreateAssetUUID derives the GUID from the path,
			// not the file content, so we can ask for the GUID before writing the
			// file at all. Single Stringify+write pass — the previous code wrote
			// the file twice (once to register the asset, once with the GUID baked
			// in) which doubled disk I/O on every prefab save.
			const uint64_t prefabGuid = AssetRegistry::GetOrCreateAssetUUID(path);

			Value root = Value::MakeObject();
			root.AddMember("version", Value(SCENE_FORMAT_VERSION));
			root.AddMember("type", Value("Prefab"));
			if (prefabGuid != 0) {
				root.AddMember("AssetGUID", Value(std::to_string(prefabGuid)));
			}
			// Both "Entity" and "prefab" — the legacy key stays in for backwards
			// compatibility with existing prefab files; the deserializer accepts either.
			root.AddMember("Entity", prefabEntity);
			root.AddMember("prefab", prefabEntity);
			File::WriteAllText(path, Json::Stringify(root, true));

			std::string name = "Entity";
			if (scene.GetRegistry().all_of<NameComponent>(entity)) {
				name = scene.GetRegistry().get<NameComponent>(entity).Name;
			}

			AIM_CORE_INFO_TAG("SceneSerializer", "Saved prefab: {}", name);
			return true;
		}
		catch (const std::exception& exception) {
			AIM_CORE_ERROR_TAG("SceneSerializer", "SaveEntityToFile failed: {}", exception.what());
			return false;
		}
	}

} // namespace Axiom

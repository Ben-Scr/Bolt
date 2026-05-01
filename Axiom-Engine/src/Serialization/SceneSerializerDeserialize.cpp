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
#include "Components/Physics/FastBody2DComponent.hpp"
#include "Components/Physics/FastBoxCollider2DComponent.hpp"
#include "Components/Physics/FastCircleCollider2DComponent.hpp"
#include "Graphics/TextureManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Physics/PhysicsTypes.hpp"
#include "Core/Log.hpp"

#include <cmath>
#include <cctype>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <vector>

namespace Axiom {

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
					AIM_CORE_WARN_TAG(
						"SceneSerializer",
						"Failed to parse managed component field metadata for {}: {}",
						className,
						parseError);
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

			return entityValue;
		}

		EntityOrigin EntityOriginFromString(const std::string& value) {
			if (value == "Scene" || value == "scene") {
				return EntityOrigin::Scene;
			}
			if (value == "Prefab" || value == "prefab") {
				return EntityOrigin::Prefab;
			}
			return EntityOrigin::Runtime;
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

		std::vector<std::string> SplitOverridePath(const std::string& path) {
			std::vector<std::string> parts;
			size_t start = 0;
			while (start <= path.size()) {
				const size_t dot = path.find('.', start);
				parts.push_back(path.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
				if (dot == std::string::npos) {
					break;
				}
				start = dot + 1;
			}
			return parts;
		}

		bool ApplyScriptFieldOverride(Value& entityValue, const std::vector<std::string>& parts, const Value& overrideValue) {
			if (parts.size() != 2) {
				return false;
			}

			Value* scriptFields = entityValue.FindMember("ScriptFields");
			if (!scriptFields || !scriptFields->IsObject()) {
				return false;
			}

			Value* classFields = scriptFields->FindMember(parts[0]);
			if (!classFields || !classFields->IsArray()) {
				return false;
			}

			for (Value& fieldValue : classFields->GetArray()) {
				if (!fieldValue.IsObject() || GetStringMember(fieldValue, "name") != parts[1]) {
					continue;
				}

				fieldValue.AddMember("value", overrideValue);
				return true;
			}

			Value fieldValue = Value::MakeObject();
			fieldValue.AddMember("name", Value(parts[1]));
			fieldValue.AddMember("value", overrideValue);
			classFields->Append(std::move(fieldValue));
			return true;
		}

		bool ApplyOverridePath(Value& entityValue, const std::string& path, const Value& overrideValue) {
			if (path.empty()) {
				return false;
			}

			const std::vector<std::string> parts = SplitOverridePath(path);
			if (parts.empty()) {
				return false;
			}

			if (ApplyScriptFieldOverride(entityValue, parts, overrideValue)) {
				return true;
			}

			Value* current = &entityValue;
			for (size_t i = 0; i + 1 < parts.size(); ++i) {
				if (!current->IsObject()) {
					return false;
				}

				Value* child = current->FindMember(parts[i]);
				if (!child) {
					child = &current->AddMember(parts[i], Value::MakeObject());
				}
				current = child;
			}

			current->AddMember(parts.back(), overrideValue);
			return true;
		}

		const Value* GetOverrideValueAtPath(const Value& entityValue, const std::string& path) {
			const std::vector<std::string> parts = SplitOverridePath(path);
			if (parts.empty()) {
				return nullptr;
			}

			if (parts.size() == 2) {
				if (const Value* scriptFields = GetObjectMember(entityValue, "ScriptFields")) {
					if (const Value* classFields = scriptFields->FindMember(parts[0]); classFields && classFields->IsArray()) {
						for (const Value& fieldValue : classFields->GetArray()) {
							if (!fieldValue.IsObject() || GetStringMember(fieldValue, "name") != parts[1]) {
								continue;
							}

							return fieldValue.FindMember("value");
						}
					}
				}
			}

			const Value* current = &entityValue;
			for (const std::string& part : parts) {
				if (!current->IsObject()) {
					return nullptr;
				}

				current = current->FindMember(part);
				if (!current) {
					return nullptr;
				}
			}
			return current;
		}

		void ApplyOverrides(Value& prefabEntityValue, const Value& instanceValue);

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

			if (const Value* prefabRefValue = root.FindMember("PrefabRef")) {
				uint64_t basePrefabGuid = 0;
				if (prefabRefValue->IsString()) {
					const std::string prefabRef = prefabRefValue->AsStringOr();
					try {
						basePrefabGuid = static_cast<uint64_t>(std::stoull(prefabRef));
					}
					catch (...) {
						basePrefabGuid = AssetRegistry::GetOrCreateAssetUUID(prefabRef);
					}
				}
				else {
					basePrefabGuid = prefabRefValue->AsUInt64Or(0);
				}

				if (basePrefabGuid != 0 && LoadPrefabEntityValue(basePrefabGuid, outEntityValue)) {
					ApplyOverrides(outEntityValue, root);
					return true;
				}
			}

			return false;
		}

		uint64_t ResolvePrefabGuid(const Value& entityValue) {
			uint64_t prefabGuid = GetUInt64Member(entityValue, "PrefabGUID", 0);
			if (prefabGuid != 0) {
				return prefabGuid;
			}

			const std::string prefabRef = GetStringMember(entityValue, "PrefabRef");
			if (!prefabRef.empty()) {
				return AssetRegistry::GetOrCreateAssetUUID(prefabRef);
			}

			return 0;
		}

		void ApplyOverrides(Value& prefabEntityValue, const Value& instanceValue) {
			if (const Value* overrides = GetObjectMember(instanceValue, "Overrides")) {
				for (const auto& [path, overrideValue] : overrides->GetObject()) {
					ApplyOverridePath(prefabEntityValue, path, overrideValue);
				}
				return;
			}

			if (const Value* overrideArray = GetArrayMember(instanceValue, "Overrides")) {
				for (const Value& overrideEntry : overrideArray->GetArray()) {
					if (!overrideEntry.IsObject()) {
						continue;
					}

					const std::string componentName = GetStringMember(overrideEntry, "Component");
					const std::string fieldName = GetStringMember(overrideEntry, "Field");
					const Value* value = overrideEntry.FindMember("Value");
					if (componentName.empty() || fieldName.empty() || !value) {
						continue;
					}

					ApplyOverridePath(prefabEntityValue, componentName + "." + fieldName, *value);
				}
			}
		}
	} // namespace

	bool SceneSerializer::LoadFromFile(Scene& scene, const std::string& path) {
		try {
			if (!File::Exists(path)) {
				AIM_CORE_WARN_TAG("SceneSerializer", "Scene file not found: {}", path);
				return false;
			}

			const std::string json = File::ReadAllText(path);
			if (json.empty()) {
				AIM_CORE_WARN_TAG("SceneSerializer", "Scene file is empty: {}", path);
				return false;
			}

			Value root;
			std::string parseError;
			if (!Json::TryParse(json, root, &parseError) || !root.IsObject()) {
				AIM_CORE_ERROR_TAG("SceneSerializer", "Failed to parse scene JSON {}: {}", path, parseError);
				return false;
			}

			return DeserializeScene(scene, root, path);
		}
		catch (const std::exception& exception) {
			AIM_CORE_ERROR_TAG("SceneSerializer", "Load failed: {}", exception.what());
			return false;
		}
	}

	bool SceneSerializer::DeserializeScene(Scene& scene, const Json::Value& root, std::string_view source) {
		if (!root.IsObject()) {
			AIM_CORE_ERROR_TAG("SceneSerializer", "DeserializeScene requires an object root");
			return false;
		}

		const int version = GetIntMember(root, "version", 1);
		if (version > SCENE_FORMAT_VERSION) {
			AIM_CORE_WARN_TAG(
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
		scene.ClearGameSystems();

		if (const Value* systemsValue = GetArrayMember(root, "systems")) {
			for (const Value& systemValue : systemsValue->GetArray()) {
				const std::string className = systemValue.AsStringOr();
				if (!className.empty()) {
					scene.AddGameSystem(className);
				}
			}
		}

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
				AIM_CORE_WARN_TAG("SceneSerializer", "No entities array in scene file: {}", sourcePath);
			}
			else {
				AIM_CORE_WARN_TAG("SceneSerializer", "No entities array in scene data");
			}
		}

		if (scene.GetRegistry().view<Camera2DComponent>().size() == 0) {
			EntityHelper::CreateCamera2DEntity(scene);
			AIM_CORE_INFO_TAG("SceneSerializer", "Added default camera (none in scene data)");
		}

		scene.ClearDirty();
		AIM_CORE_INFO_TAG("SceneSerializer", "Loaded scene: {}", scene.GetName());
		return true;
	}

	EntityHandle SceneSerializer::DeserializeEntity(Scene& scene, const Json::Value& entityValue) {
		if (!entityValue.IsObject()) {
			return entt::null;
		}

		const std::string originText = GetStringMember(entityValue, "Origin", "Scene");
		const EntityOrigin origin = EntityOriginFromString(originText);
		if (origin == EntityOrigin::Runtime) {
			return entt::null;
		}
		if (origin == EntityOrigin::Prefab) {
			return DeserializePrefabInstance(scene, entityValue);
		}

		return DeserializeFullEntity(scene, entityValue, EntityOrigin::Scene);
	}

	EntityHandle SceneSerializer::DeserializeFullEntity(
		Scene& scene,
		const Json::Value& entityValue,
		EntityOrigin origin,
		uint64_t prefabGuid) {
		if (!entityValue.IsObject()) {
			return entt::null;
		}

		const std::string name = GetStringMember(entityValue, "name", "Entity");
		const EntityHandle entity = scene.CreateEntity(name).GetHandle();

		uint64_t savedSceneGuid = GetUInt64Member(entityValue, "SceneGUID", 0);
		if (savedSceneGuid == 0) {
			savedSceneGuid = GetUInt64Member(entityValue, "uuid", 0);
		}
		const uint64_t savedRuntimeId = GetUInt64Member(entityValue, "RuntimeID", 0);
		scene.SetEntityMetaData(
			entity,
			origin,
			AssetGUID(prefabGuid),
			AssetGUID(savedSceneGuid),
			savedRuntimeId);

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

		if (const Value* bodyValue = GetObjectMember(entityValue, "FastBody2D")) {
			auto& body = scene.AddComponent<FastBody2DComponent>(entity);
			body.Type = static_cast<AxiomPhys::BodyType>(GetIntMember(*bodyValue, "type", 1));
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

		if (const Value* colliderValue = GetObjectMember(entityValue, "FastBoxCollider2D")) {
			auto& collider = scene.AddComponent<FastBoxCollider2DComponent>(entity);
			collider.HalfExtents = {
				GetFloatMember(*colliderValue, "halfX", 0.5f),
				GetFloatMember(*colliderValue, "halfY", 0.5f)
			};
			if (collider.m_Collider) {
				collider.m_Collider->SetHalfExtents({ collider.HalfExtents.x, collider.HalfExtents.y });
			}
		}

		if (const Value* colliderValue = GetObjectMember(entityValue, "FastCircleCollider2D")) {
			auto& collider = scene.AddComponent<FastCircleCollider2DComponent>(entity);
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

		ScriptComponent* scriptComponent = nullptr;
		auto getOrCreateScriptComponent = [&]() -> ScriptComponent& {
			if (!scriptComponent) {
				if (scene.HasComponent<ScriptComponent>(entity)) {
					scriptComponent = &scene.GetComponent<ScriptComponent>(entity);
				}
				else {
					scriptComponent = &scene.AddComponent<ScriptComponent>(entity);
				}
			}
			return *scriptComponent;
		};

		if (const Value* scriptsValue = GetArrayMember(entityValue, "Scripts")) {
			auto& scripts = getOrCreateScriptComponent();
			for (const Value& scriptValue : scriptsValue->GetArray()) {
				if (scriptValue.IsString()) {
					scripts.AddScript(scriptValue.AsStringOr(), ScriptType::Unknown);
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

				scripts.AddScript(
					className,
					ScriptTypeFromString(GetStringMember(scriptValue, "type")));
			}
		}

		if (const Value* componentsValue = GetArrayMember(entityValue, "ManagedComponents")) {
			auto& components = getOrCreateScriptComponent();
			for (const Value& componentValue : componentsValue->GetArray()) {
				if (componentValue.IsString()) {
					components.AddManagedComponent(componentValue.AsStringOr());
					continue;
				}

				if (!componentValue.IsObject()) {
					continue;
				}

				std::string className = GetStringMember(componentValue, "className");
				if (className.empty()) {
					className = GetStringMember(componentValue, "class");
				}
				if (!className.empty()) {
					components.AddManagedComponent(className);
				}
			}
		}

		if (const Value* fieldsByClass = GetObjectMember(entityValue, "ScriptFields")) {
			if (scriptComponent || scene.HasComponent<ScriptComponent>(entity)) {
				auto& fieldsComponent = getOrCreateScriptComponent();
				for (const auto& [className, fieldsValue] : fieldsByClass->GetObject()) {
					if ((!fieldsComponent.HasScript(className) && !fieldsComponent.HasManagedComponent(className))
						|| !fieldsValue.IsArray()) {
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

						fieldsComponent.PendingFieldValues[className + "." + fieldName] =
							ValueToFieldString(*valueValue);
					}
				}
			}
		}

		return entity;
	}

	EntityHandle SceneSerializer::DeserializeEntityFromValue(Scene& scene, const Json::Value& entityValue) {
		if (!entityValue.IsObject()) {
			return entt::null;
		}

		Value entityCopy = entityValue;
		RemoveEntityIdentityMembers(entityCopy);
		return DeserializeFullEntity(scene, entityCopy, EntityOrigin::Scene);
	}

	bool SceneSerializer::DeserializeComponent(Scene& scene, EntityHandle entity, std::string_view componentName, const Json::Value& componentValue) {
		if (entity == entt::null || !scene.IsValid(entity) || componentName.empty() || !componentValue.IsObject()) {
			return false;
		}

		const std::string component(componentName);

		if (component == "Transform2D") {
			auto& transform = scene.HasComponent<Transform2DComponent>(entity)
				? scene.GetComponent<Transform2DComponent>(entity)
				: scene.AddComponent<Transform2DComponent>(entity);
			transform.Position.x = GetFloatMember(componentValue, "posX", 0.0f);
			transform.Position.y = GetFloatMember(componentValue, "posY", 0.0f);
			transform.Rotation = GetFloatMember(componentValue, "rotation", 0.0f);
			transform.Scale.x = GetFloatMember(componentValue, "scaleX", 1.0f);
			transform.Scale.y = GetFloatMember(componentValue, "scaleY", 1.0f);
			transform.MarkDirty();
			return true;
		}

		if (component == "SpriteRenderer") {
			auto& spriteRenderer = scene.HasComponent<SpriteRendererComponent>(entity)
				? scene.GetComponent<SpriteRendererComponent>(entity)
				: scene.AddComponent<SpriteRendererComponent>(entity);
			spriteRenderer.Color.r = GetFloatMember(componentValue, "r", 1.0f);
			spriteRenderer.Color.g = GetFloatMember(componentValue, "g", 1.0f);
			spriteRenderer.Color.b = GetFloatMember(componentValue, "b", 1.0f);
			spriteRenderer.Color.a = GetFloatMember(componentValue, "a", 1.0f);
			spriteRenderer.SortingOrder = static_cast<short>(GetIntMember(componentValue, "sortOrder", 0));
			spriteRenderer.SortingLayer = static_cast<uint8_t>(GetIntMember(componentValue, "sortLayer", 0));
			const Filter filter = static_cast<Filter>(GetIntMember(componentValue, "filter", static_cast<int>(Filter::Point)));
			const Wrap wrapU = static_cast<Wrap>(GetIntMember(componentValue, "wrapU", static_cast<int>(Wrap::Clamp)));
			const Wrap wrapV = static_cast<Wrap>(GetIntMember(componentValue, "wrapV", static_cast<int>(Wrap::Clamp)));
			spriteRenderer.TextureHandle = LoadTextureFromValue(
				componentValue,
				"textureAsset",
				"texture",
				filter,
				wrapU,
				wrapV,
				&spriteRenderer.TextureAssetId);
			return true;
		}

		if (component == "Rigidbody2D") {
			auto& rigidbody = scene.HasComponent<Rigidbody2DComponent>(entity)
				? scene.GetComponent<Rigidbody2DComponent>(entity)
				: scene.AddComponent<Rigidbody2DComponent>(entity);
			rigidbody.SetBodyType(static_cast<BodyType>(GetIntMember(componentValue, "bodyType", static_cast<int>(BodyType::Dynamic))));
			rigidbody.SetGravityScale(GetFloatMember(componentValue, "gravityScale", 1.0f));
			rigidbody.SetMass(GetFloatMember(componentValue, "mass", 1.0f));
			return true;
		}

		if (component == "BoxCollider2D") {
			auto& boxCollider = scene.HasComponent<BoxCollider2DComponent>(entity)
				? scene.GetComponent<BoxCollider2DComponent>(entity)
				: scene.AddComponent<BoxCollider2DComponent>(entity);
			const Vec2 savedCenter{
				GetFloatMember(componentValue, "centerX", 0.0f),
				GetFloatMember(componentValue, "centerY", 0.0f)
			};
			boxCollider.SetCenter(savedCenter, scene);

			const auto& transform = scene.GetComponent<Transform2DComponent>(entity);
			const Vec2 savedScale{
				GetFloatMember(componentValue, "scaleX", transform.Scale.x),
				GetFloatMember(componentValue, "scaleY", transform.Scale.y)
			};
			Vec2 localScale{ 1.0f, 1.0f };
			if (std::fabs(transform.Scale.x) > k_MinScaleAxis) {
				localScale.x = savedScale.x / transform.Scale.x;
			}
			if (std::fabs(transform.Scale.y) > k_MinScaleAxis) {
				localScale.y = savedScale.y / transform.Scale.y;
			}
			boxCollider.SetScale(localScale, scene);
			boxCollider.SetSensor(GetBoolMember(componentValue, "sensor", false), scene);
			boxCollider.SetFriction(GetFloatMember(componentValue, "friction", boxCollider.GetFriction()));
			boxCollider.SetBounciness(GetFloatMember(componentValue, "bounciness", boxCollider.GetBounciness()));
			boxCollider.SetLayer(GetUInt64Member(componentValue, "layer", boxCollider.GetLayer()));
			boxCollider.SetRegisterContacts(GetBoolMember(componentValue, "registerContacts", boxCollider.CanRegisterContacts()));
			return true;
		}

		if (component == "AudioSource") {
			auto& audioSource = scene.HasComponent<AudioSourceComponent>(entity)
				? scene.GetComponent<AudioSourceComponent>(entity)
				: scene.AddComponent<AudioSourceComponent>(entity);
			audioSource.SetVolume(GetFloatMember(componentValue, "volume", 1.0f));
			audioSource.SetPitch(GetFloatMember(componentValue, "pitch", 1.0f));
			audioSource.SetLoop(GetBoolMember(componentValue, "loop", false));
			audioSource.SetPlayOnAwake(GetBoolMember(componentValue, "playOnAwake", false));

			UUID audioAssetId = UUID(0);
			const AudioHandle handle = LoadAudioFromValue(componentValue, "clipAsset", "clip", &audioAssetId);
			audioSource.SetAudioHandle(handle, audioAssetId);
			return true;
		}

		if (component == "Camera2D") {
			auto& camera = scene.HasComponent<Camera2DComponent>(entity)
				? scene.GetComponent<Camera2DComponent>(entity)
				: scene.AddComponent<Camera2DComponent>(entity);
			camera.SetOrthographicSize(GetFloatMember(componentValue, "orthoSize", 5.0f));
			camera.SetZoom(GetFloatMember(componentValue, "zoom", 1.0f));
			camera.SetClearColor(Color(
				GetFloatMember(componentValue, "clearR", 0.1f),
				GetFloatMember(componentValue, "clearG", 0.1f),
				GetFloatMember(componentValue, "clearB", 0.1f),
				GetFloatMember(componentValue, "clearA", 1.0f)));
			return true;
		}

		if (component == "FastBody2D") {
			auto& body = scene.HasComponent<FastBody2DComponent>(entity)
				? scene.GetComponent<FastBody2DComponent>(entity)
				: scene.AddComponent<FastBody2DComponent>(entity);
			body.Type = static_cast<AxiomPhys::BodyType>(GetIntMember(componentValue, "type", 1));
			body.Mass = GetFloatMember(componentValue, "mass", 1.0f);
			body.UseGravity = GetBoolMember(componentValue, "useGravity", true);
			body.BoundaryCheck = GetBoolMember(componentValue, "boundaryCheck", false);

			if (body.m_Body) {
				body.m_Body->SetBodyType(body.Type);
				body.m_Body->SetMass(body.Mass);
				body.m_Body->SetGravityEnabled(body.UseGravity);
				body.m_Body->SetBoundaryCheckEnabled(body.BoundaryCheck);
			}
			return true;
		}

		if (component == "FastBoxCollider2D") {
			auto& collider = scene.HasComponent<FastBoxCollider2DComponent>(entity)
				? scene.GetComponent<FastBoxCollider2DComponent>(entity)
				: scene.AddComponent<FastBoxCollider2DComponent>(entity);
			collider.HalfExtents = {
				GetFloatMember(componentValue, "halfX", 0.5f),
				GetFloatMember(componentValue, "halfY", 0.5f)
			};
			if (collider.m_Collider) {
				collider.m_Collider->SetHalfExtents({ collider.HalfExtents.x, collider.HalfExtents.y });
			}
			return true;
		}

		if (component == "FastCircleCollider2D") {
			auto& collider = scene.HasComponent<FastCircleCollider2DComponent>(entity)
				? scene.GetComponent<FastCircleCollider2DComponent>(entity)
				: scene.AddComponent<FastCircleCollider2DComponent>(entity);
			collider.Radius = GetFloatMember(componentValue, "radius", 0.5f);
			if (collider.m_Collider) {
				collider.m_Collider->SetRadius(collider.Radius);
			}
			return true;
		}

		if (component == "ParticleSystem2D") {
			auto& particleSystem = scene.HasComponent<ParticleSystem2DComponent>(entity)
				? scene.GetComponent<ParticleSystem2DComponent>(entity)
				: scene.AddComponent<ParticleSystem2DComponent>(entity);
			particleSystem.PlayOnAwake = GetBoolMember(componentValue, "playOnAwake", true);
			particleSystem.ParticleSettings.LifeTime = GetFloatMember(componentValue, "lifetime", 1.0f);
			particleSystem.ParticleSettings.Speed = GetFloatMember(componentValue, "speed", 5.0f);
			particleSystem.ParticleSettings.Scale = GetFloatMember(componentValue, "scale", 1.0f);
			particleSystem.ParticleSettings.Gravity.x = GetFloatMember(componentValue, "gravityX", 0.0f);
			particleSystem.ParticleSettings.Gravity.y = GetFloatMember(componentValue, "gravityY", 0.0f);
			particleSystem.ParticleSettings.UseGravity = GetBoolMember(componentValue, "useGravity", false);
			particleSystem.ParticleSettings.UseRandomColors = GetBoolMember(componentValue, "useRandomColors", false);
			particleSystem.ParticleSettings.MoveDirection.x = GetFloatMember(componentValue, "moveDirectionX", 0.0f);
			particleSystem.ParticleSettings.MoveDirection.y = GetFloatMember(componentValue, "moveDirectionY", 0.0f);
			particleSystem.EmissionSettings.EmitOverTime =
				static_cast<uint16_t>(GetIntMember(componentValue, "emitOverTime", 10));
			particleSystem.EmissionSettings.RateOverDistance =
				static_cast<uint16_t>(GetIntMember(componentValue, "rateOverDistance", 0));
			particleSystem.EmissionSettings.EmissionSpace = static_cast<ParticleSystem2DComponent::Space>(
				GetIntMember(componentValue, "emissionSpace", static_cast<int>(ParticleSystem2DComponent::Space::World)));

			if (GetIntMember(componentValue, "shapeType", 0) == 0) {
				ParticleSystem2DComponent::CircleParams circle;
				circle.Radius = GetFloatMember(componentValue, "radius", 1.0f);
				circle.IsOnCircle = GetBoolMember(componentValue, "isOnCircle", false);
				particleSystem.Shape = circle;
			}
			else {
				ParticleSystem2DComponent::SquareParams square;
				square.HalfExtends.x = GetFloatMember(componentValue, "halfExtendsX", 1.0f);
				square.HalfExtends.y = GetFloatMember(componentValue, "halfExtendsY", 1.0f);
				particleSystem.Shape = square;
			}

			particleSystem.RenderingSettings.MaxParticles =
				static_cast<uint32_t>(GetUInt64Member(componentValue, "maxParticles", 1000));
			particleSystem.RenderingSettings.Color.r = GetFloatMember(componentValue, "colorR", 1.0f);
			particleSystem.RenderingSettings.Color.g = GetFloatMember(componentValue, "colorG", 1.0f);
			particleSystem.RenderingSettings.Color.b = GetFloatMember(componentValue, "colorB", 1.0f);
			particleSystem.RenderingSettings.Color.a = GetFloatMember(componentValue, "colorA", 1.0f);
			particleSystem.RenderingSettings.SortingOrder =
				static_cast<short>(GetIntMember(componentValue, "sortOrder", 0));
			particleSystem.RenderingSettings.SortingLayer =
				static_cast<uint8_t>(GetIntMember(componentValue, "sortLayer", 0));

			UUID textureAssetId = UUID(0);
			const TextureHandle textureHandle = LoadTextureFromValue(
				componentValue,
				"textureAsset",
				"texture",
				Filter::Point,
				Wrap::Clamp,
				Wrap::Clamp,
				&textureAssetId);
			particleSystem.SetTexture(textureHandle, textureAssetId);
			return true;
		}

		if (component == "RectTransform") {
			auto& rectTransform = scene.HasComponent<RectTransformComponent>(entity)
				? scene.GetComponent<RectTransformComponent>(entity)
				: scene.AddComponent<RectTransformComponent>(entity);
			rectTransform.Position.x = GetFloatMember(componentValue, "posX", 0.0f);
			rectTransform.Position.y = GetFloatMember(componentValue, "posY", 0.0f);
			rectTransform.Pivot.x = GetFloatMember(componentValue, "pivotX", 0.0f);
			rectTransform.Pivot.y = GetFloatMember(componentValue, "pivotY", 0.0f);
			rectTransform.Width = GetFloatMember(componentValue, "width", 100.0f);
			rectTransform.Height = GetFloatMember(componentValue, "height", 100.0f);
			rectTransform.Rotation = GetFloatMember(componentValue, "rotation", 0.0f);
			rectTransform.Scale.x = GetFloatMember(componentValue, "scaleX", 1.0f);
			rectTransform.Scale.y = GetFloatMember(componentValue, "scaleY", 1.0f);
			return true;
		}

		if (component == "Image") {
			auto& image = scene.HasComponent<ImageComponent>(entity)
				? scene.GetComponent<ImageComponent>(entity)
				: scene.AddComponent<ImageComponent>(entity);
			image.Color.r = GetFloatMember(componentValue, "r", 1.0f);
			image.Color.g = GetFloatMember(componentValue, "g", 1.0f);
			image.Color.b = GetFloatMember(componentValue, "b", 1.0f);
			image.Color.a = GetFloatMember(componentValue, "a", 1.0f);

			image.TextureHandle = LoadTextureFromValue(
				componentValue,
				"textureAsset",
				"texture",
				Filter::Point,
				Wrap::Clamp,
				Wrap::Clamp,
				&image.TextureAssetId);
			return true;
		}

		return false;
	}

	bool SceneSerializer::ResetComponent(Scene& scene, EntityHandle entity, std::string_view componentName) {
		Value defaults = Value::MakeObject();
		return DeserializeComponent(scene, entity, componentName, defaults);
	}

	EntityHandle SceneSerializer::DeserializePrefabInstance(Scene& scene, const Json::Value& entityValue) {
		const uint64_t prefabGuid = ResolvePrefabGuid(entityValue);
		if (prefabGuid == 0) {
			AIM_CORE_WARN_TAG("SceneSerializer", "Prefab instance missing PrefabGUID");
			return entt::null;
		}

		Value prefabEntityValue;
		if (!LoadPrefabEntityValue(prefabGuid, prefabEntityValue)) {
			AIM_CORE_WARN_TAG("SceneSerializer", "Could not load prefab {}", prefabGuid);
			return entt::null;
		}

		ApplyOverrides(prefabEntityValue, entityValue);
		return DeserializeFullEntity(scene, prefabEntityValue, EntityOrigin::Prefab, prefabGuid);
	}

	EntityHandle SceneSerializer::InstantiatePrefab(Scene& scene, uint64_t prefabGuid) {
		if (prefabGuid == 0 || AssetRegistry::GetKind(prefabGuid) != AssetKind::Prefab) {
			return entt::null;
		}

		Value prefabEntityValue;
		if (!LoadPrefabEntityValue(prefabGuid, prefabEntityValue)) {
			return entt::null;
		}

		RemoveEntityIdentityMembers(prefabEntityValue);
		return DeserializeFullEntity(scene, prefabEntityValue, EntityOrigin::Prefab, prefabGuid);
	}

	bool SceneSerializer::ApplyPrefabInstanceOverrides(Scene& scene, EntityHandle entity) {
		if (entity == entt::null || !scene.IsValid(entity) || scene.GetEntityOrigin(entity) != EntityOrigin::Prefab) {
			return false;
		}

		const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entity));
		const std::string prefabPath = AssetRegistry::ResolvePath(prefabGuid);
		if (prefabGuid == 0 || prefabPath.empty()) {
			return false;
		}

		Value prefabEntity = SerializeEntityFull(scene, entity);
		RemoveEntityIdentityMembers(prefabEntity);

		Value root = Value::MakeObject();
		root.AddMember("version", Value(SCENE_FORMAT_VERSION));
		root.AddMember("type", Value("Prefab"));
		root.AddMember("AssetGUID", Value(std::to_string(prefabGuid)));
		root.AddMember("Entity", prefabEntity);
		root.AddMember("prefab", prefabEntity);

		try {
			File::WriteAllText(prefabPath, Json::Stringify(root, true));
			AssetRegistry::MarkDirty();
			AssetRegistry::Sync();
			scene.MarkDirty();
			return true;
		}
		catch (const std::exception& exception) {
			AIM_CORE_ERROR_TAG("SceneSerializer", "ApplyPrefabInstanceOverrides failed: {}", exception.what());
			return false;
		}
	}

	EntityHandle SceneSerializer::RevertPrefabInstanceOverride(
		Scene& scene,
		EntityHandle entity,
		const std::string& overridePath) {
		if (entity == entt::null || !scene.IsValid(entity) || scene.GetEntityOrigin(entity) != EntityOrigin::Prefab) {
			return entt::null;
		}

		const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entity));
		Value prefabEntityValue;
		if (prefabGuid == 0 || !LoadPrefabEntityValue(prefabGuid, prefabEntityValue)) {
			return entt::null;
		}

		Value replacementValue;
		if (overridePath.empty()) {
			replacementValue = prefabEntityValue;
		}
		else {
			replacementValue = SerializeEntityFull(scene, entity);
			RemoveEntityIdentityMembers(replacementValue);

			const Value* baseValue = GetOverrideValueAtPath(prefabEntityValue, overridePath);
			if (!baseValue || !ApplyOverridePath(replacementValue, overridePath, *baseValue)) {
				return entt::null;
			}
		}

		RemoveEntityIdentityMembers(replacementValue);
		EntityHandle replacement = DeserializeFullEntity(scene, replacementValue, EntityOrigin::Prefab, prefabGuid);
		if (replacement == entt::null) {
			return entt::null;
		}

		scene.DestroyEntity(entity);
		scene.MarkDirty();
		return replacement;
	}

	EntityHandle SceneSerializer::LoadEntityFromFile(Scene& scene, const std::string& path) {
		try {
			if (!File::Exists(path)) {
				AIM_CORE_WARN_TAG("SceneSerializer", "Prefab file not found: {}", path);
				return entt::null;
			}

			const uint64_t prefabGuid = AssetRegistry::GetOrCreateAssetUUID(path);
			if (prefabGuid != 0 && AssetRegistry::GetKind(prefabGuid) == AssetKind::Prefab) {
				EntityHandle instance = InstantiatePrefab(scene, prefabGuid);
				if (instance != entt::null) {
					return instance;
				}
			}

			const std::string json = File::ReadAllText(path);
			if (json.empty()) {
				AIM_CORE_WARN_TAG("SceneSerializer", "Prefab file is empty: {}", path);
				return entt::null;
			}

			Value root;
			std::string parseError;
			if (!Json::TryParse(json, root, &parseError) || !root.IsObject()) {
				AIM_CORE_ERROR_TAG("SceneSerializer", "Failed to parse prefab JSON {}: {}", path, parseError);
				return entt::null;
			}

			const Value* prefabValue = GetObjectMember(root, "Entity");
			if (!prefabValue) {
				prefabValue = GetObjectMember(root, "prefab");
			}
			if (!prefabValue) {
				AIM_CORE_WARN_TAG("SceneSerializer", "No prefab block in file: {}", path);
				return entt::null;
			}

			return DeserializeFullEntity(scene, *prefabValue, EntityOrigin::Prefab, prefabGuid);
		}
		catch (const std::exception& exception) {
			AIM_CORE_ERROR_TAG("SceneSerializer", "LoadEntityFromFile failed: {}", exception.what());
			return entt::null;
		}
	}

} // namespace Axiom

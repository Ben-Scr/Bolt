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
					BT_CORE_WARN_TAG("SceneSerializer", "Failed to parse managed component field metadata for {}: {}", className, parseError);
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
	} // namespace

	Json::Value SceneSerializer::SerializeScene(Scene& scene) {
		Value root = Value::MakeObject();
		root.AddMember("version", Value(SCENE_FORMAT_VERSION));
		root.AddMember("name", Value(scene.GetName()));
		root.AddMember("sceneId", Value(std::to_string(static_cast<uint64_t>(scene.GetSceneId()))));

		Value entitiesValue = Value::MakeArray();
		auto& registry = scene.GetRegistry();
		auto view = registry.view<entt::entity>();
		std::vector<entt::entity> entities(view.begin(), view.end());

		for (const entt::entity entity : entities) {
			entitiesValue.Append(SerializeEntity(scene, entity));
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
			scene.ClearDirty();
			BT_CORE_INFO_TAG("SceneSerializer", "Saved scene: {}", scene.GetName());
			return true;
		}
		catch (const std::exception& exception) {
			BT_CORE_ERROR_TAG("SceneSerializer", "Save failed: {}", exception.what());
			return false;
		}
	}

	bool SceneSerializer::SaveEntityToFile(Scene& scene, EntityHandle entity, const std::string& path) {
		try {
			const std::filesystem::path parentDir = std::filesystem::path(path).parent_path();
			if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
				std::filesystem::create_directories(parentDir);
			}

			Value root = Value::MakeObject();
			root.AddMember("prefab", SerializeEntity(scene, entity));
			File::WriteAllText(path, Json::Stringify(root, true));

			std::string name = "Entity";
			if (scene.GetRegistry().all_of<NameComponent>(entity)) {
				name = scene.GetRegistry().get<NameComponent>(entity).Name;
			}

			BT_CORE_INFO_TAG("SceneSerializer", "Saved prefab: {}", name);
			return true;
		}
		catch (const std::exception& exception) {
			BT_CORE_ERROR_TAG("SceneSerializer", "SaveEntityToFile failed: {}", exception.what());
			return false;
		}
	}

} // namespace Bolt

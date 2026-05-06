#include "pch.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Audio/AudioManager.hpp"
#include "Components/Components.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Graphics/Text/FontManager.hpp"
#include "Graphics/TextureManager.hpp"
#include "Inspector/PropertyRegistration.hpp"
#include "Physics/PhysicsTypes.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptComponent.hpp"

#include <cmath>

#include <vector>

// Single source of truth for built-in components — names, categories, properties.
namespace Axiom {
	namespace {
		template<typename T>
		void RegisterComponent(SceneManager& sceneManager, const std::string& displayName,
			ComponentCategory category = ComponentCategory::Component,
			const std::string& subcategory = "",
			const std::string& serializedName = "",
			std::vector<PropertyDescriptor> properties = {})
		{
			ComponentInfo info{ displayName, subcategory, category };
			info.serializedName = serializedName;
			info.properties = std::move(properties);
			sceneManager.RegisterComponentType<T>(info);
		}

		template<typename A, typename B>
		void DeclareConflict(SceneManager& sceneManager) {
			const std::type_index aId(typeid(A));
			const std::type_index bId(typeid(B));
			sceneManager.GetComponentRegistry().ForEachComponentInfo(
				[&](const std::type_index& id, ComponentInfo& info) {
					if (id == aId) {
						bool present = false;
						for (const auto& c : info.conflictsWith) if (c == bId) { present = true; break; }
						if (!present) info.conflictsWith.push_back(bId);
					}
					else if (id == bId) {
						bool present = false;
						for (const auto& c : info.conflictsWith) if (c == aId) { present = true; break; }
						if (!present) info.conflictsWith.push_back(aId);
					}
				});
		}

		// ── Texture / audio ref helpers ─────────────────────────────
		template <typename T, typename HandleAccessor, typename HandleAssign>
		PropertyDescriptor MakeTextureRefDirect(const std::string& name, const std::string& displayName,
			HandleAccessor getHandle, HandleAssign setHandle)
		{
			return Properties::MakeTextureRef(name, displayName,
				[getHandle](const Entity& e) -> uint64_t {
					const auto& component = e.GetComponent<T>();
					if constexpr (requires { component.TextureAssetId; }) {
						const uint64_t assetId = static_cast<uint64_t>(component.TextureAssetId);
						if (assetId != 0) {
							return assetId;
						}
					}
					const auto handle = getHandle(component);
					return handle.IsValid() ? TextureManager::GetTextureAssetUUID(handle) : 0ull;
				},
				[setHandle](Entity& e, uint64_t uuid) {
					auto& component = e.GetComponent<T>();
					TextureHandle h = uuid != 0
						? TextureManager::LoadTextureByUUID(uuid)
						: TextureHandle{};
					if (uuid != 0 && !h.IsValid()) {
						AssetRegistry::MarkDirty();
						AssetRegistry::Sync();
						h = TextureManager::LoadTextureByUUID(uuid);
					}
					setHandle(component, h, UUID(uuid));
				});
		}
	}

	void RegisterBuiltInComponents(SceneManager& sceneManager) {
		// ── General ─────────────────────────────────────────────────

		RegisterComponent<Transform2DComponent>(sceneManager, "Transform 2D",
			ComponentCategory::Component, "General", "Transform2D",
			{
				Properties::MakeWith<Vec2>("Position", "Position",
					[](const Entity& e) { return e.GetComponent<Transform2DComponent>().Position; },
					[](Entity& e, const Vec2& v) {
						e.GetComponent<Transform2DComponent>().SetPosition(v);
					}),
				Properties::MakeWith<float>("Rotation", "Rotation",
					[](const Entity& e) { return e.GetComponent<Transform2DComponent>().Rotation; },
					[](Entity& e, float v) {
						e.GetComponent<Transform2DComponent>().SetRotation(v);
					},
					[]() { PropertyMetadata m; m.DragSpeed = 0.01f; return m; }()),
				Properties::MakeWith<Vec2>("Scale", "Scale",
					[](const Entity& e) { return e.GetComponent<Transform2DComponent>().Scale; },
					[](Entity& e, const Vec2& v) {
						e.GetComponent<Transform2DComponent>().SetScale(v);
					}),
			});

		RegisterComponent<RectTransformComponent>(sceneManager, "Rect Transform",
			ComponentCategory::Component, "UI", "RectTransform",
			{
				Properties::Make("AnchorMin",        "Anchor Min",        &RectTransformComponent::AnchorMin),
				Properties::Make("AnchorMax",        "Anchor Max",        &RectTransformComponent::AnchorMax),
				Properties::Make("Pivot",            "Pivot",             &RectTransformComponent::Pivot),
				Properties::Make("AnchoredPosition", "Anchored Position", &RectTransformComponent::AnchoredPosition),
				Properties::Make("SizeDelta",        "Size",              &RectTransformComponent::SizeDelta),
				Properties::Make("Rotation",         "Rotation",          &RectTransformComponent::Rotation),
				Properties::Make("Scale",            "Scale",             &RectTransformComponent::Scale),
			});

		RegisterComponent<NameComponent>(sceneManager, "Name",
			ComponentCategory::Component, "General", "name",
			{
				Properties::Make("Name", "Name", &NameComponent::Name),
			});

		RegisterComponent<UUIDComponent>(sceneManager, "UUID", ComponentCategory::Tag);
		RegisterComponent<EntityMetaDataComponent>(sceneManager, "Entity Metadata", ComponentCategory::Tag);
		RegisterComponent<PrefabInstanceComponent>(sceneManager, "Prefab Instance", ComponentCategory::Tag);

		// ── Rendering ───────────────────────────────────────────────

		RegisterComponent<SpriteRendererComponent>(sceneManager, "Sprite Renderer",
			ComponentCategory::Component, "Rendering", "SpriteRenderer",
			{
				Properties::Make("Color", "Color", &SpriteRendererComponent::Color),
				Properties::MakeWith<int16_t>("SortingOrder", "Sorting Order",
					[](const Entity& e) { return e.GetComponent<SpriteRendererComponent>().SortingOrder; },
					[](Entity& e, int16_t v) {
						e.GetComponent<SpriteRendererComponent>().SortingOrder = v;
					}),
				Properties::MakeWith<uint8_t>("SortingLayer", "Sorting Layer",
					[](const Entity& e) { return e.GetComponent<SpriteRendererComponent>().SortingLayer; },
					[](Entity& e, uint8_t v) {
						e.GetComponent<SpriteRendererComponent>().SortingLayer = v;
					}),
				MakeTextureRefDirect<SpriteRendererComponent>("Texture", "Texture",
					[](const SpriteRendererComponent& s) { return s.TextureHandle; },
					[](SpriteRendererComponent& s, TextureHandle h, UUID assetId) {
						s.TextureHandle = h;
						s.TextureAssetId = assetId;
					}),
			});

		RegisterComponent<ImageComponent>(sceneManager, "Image",
			ComponentCategory::Component, "UI", "Image",
			{
				Properties::Make("Color", "Color", &ImageComponent::Color),
				MakeTextureRefDirect<ImageComponent>("Texture", "Texture",
					[](const ImageComponent& i) { return i.TextureHandle; },
					[](ImageComponent& i, TextureHandle h, UUID assetId) {
						i.TextureHandle = h;
						i.TextureAssetId = assetId;
					}),
			});

		RegisterComponent<Camera2DComponent>(sceneManager, "Camera 2D",
			ComponentCategory::Component, "Rendering", "Camera2D",
			{
				Properties::MakeWith<float>("Zoom", "Zoom",
					[](const Entity& e) { return e.GetComponent<Camera2DComponent>().GetZoom(); },
					[](Entity& e, float v) {
						e.GetComponent<Camera2DComponent>().SetZoom(v);
					},
					[]() { PropertyMetadata m; m.HasClamp = true; m.ClampMin = 0.01; m.ClampMax = 100.0; m.DragSpeed = 0.01f; return m; }()),
				Properties::MakeWith<float>("OrthographicSize", "Orthographic Size",
					[](const Entity& e) { return e.GetComponent<Camera2DComponent>().GetOrthographicSize(); },
					[](Entity& e, float v) {
						e.GetComponent<Camera2DComponent>().SetOrthographicSize(v);
					},
					[]() { PropertyMetadata m; m.HasClamp = true; m.ClampMin = 0.05; m.ClampMax = 1000.0; m.DragSpeed = 0.05f; return m; }()),
				Properties::MakeWith<Color>("ClearColor", "Clear Color",
					[](const Entity& e) { return e.GetComponent<Camera2DComponent>().GetClearColor(); },
					[](Entity& e, const Color& c) {
						e.GetComponent<Camera2DComponent>().SetClearColor(c);
					}),
			});

		// Custom drawInspector — variant shapes / play-pause don't map to PropertyDescriptors.
		RegisterComponent<ParticleSystem2DComponent>(sceneManager, "Particle System 2D",
			ComponentCategory::Component, "Rendering", "ParticleSystem2D");

		RegisterComponent<TextRendererComponent>(sceneManager, "Text Renderer",
			ComponentCategory::Component, "UI", "TextRenderer",
			{
				Properties::Make("Text", "Text", &TextRendererComponent::Text),
				Properties::MakeFontRef("Font", "Font",
					[](const Entity& e) -> uint64_t {
						return static_cast<uint64_t>(e.GetComponent<TextRendererComponent>().FontAssetId);
					},
					[](Entity& e, uint64_t uuid) {
						auto& text = e.GetComponent<TextRendererComponent>();
						text.FontAssetId = UUID(uuid);
						// Force re-resolve next frame — atlas might need
						// a different size if FontSize changed at the same
						// time, but ResolvedFont = invalid is enough either way.
						text.ResolvedFont = FontHandle{};
					}),
				Properties::MakeWith<float>("FontSize", "Font Size",
					[](const Entity& e) { return e.GetComponent<TextRendererComponent>().FontSize; },
					[](Entity& e, float v) {
						auto& text = e.GetComponent<TextRendererComponent>();
						text.FontSize = v;
						// Different px size means different atlas slot.
						text.ResolvedFont = FontHandle{};
					},
					[]() { PropertyMetadata m; m.HasClamp = true; m.ClampMin = 1.0; m.ClampMax = 512.0; m.DragSpeed = 0.5f; return m; }()),
				Properties::Make("Color", "Color", &TextRendererComponent::Color),
				Properties::MakeWith<float>("LetterSpacing", "Letter Spacing",
					[](const Entity& e) { return e.GetComponent<TextRendererComponent>().LetterSpacing; },
					[](Entity& e, float v) {
						e.GetComponent<TextRendererComponent>().LetterSpacing = v;
					},
					[]() { PropertyMetadata m; m.HasClamp = true; m.ClampMin = -64.0; m.ClampMax = 64.0; m.DragSpeed = 0.1f; return m; }()),
				Properties::MakeWith<TextAlignment>("Alignment", "Alignment",
					[](const Entity& e) { return e.GetComponent<TextRendererComponent>().HAlign; },
					[](Entity& e, TextAlignment v) {
						e.GetComponent<TextRendererComponent>().HAlign = v;
					}),
				Properties::MakeWith<int16_t>("SortingOrder", "Sorting Order",
					[](const Entity& e) { return e.GetComponent<TextRendererComponent>().SortingOrder; },
					[](Entity& e, int16_t v) {
						e.GetComponent<TextRendererComponent>().SortingOrder = v;
					}),
				Properties::MakeWith<uint8_t>("SortingLayer", "Sorting Layer",
					[](const Entity& e) { return e.GetComponent<TextRendererComponent>().SortingLayer; },
					[](Entity& e, uint8_t v) {
						e.GetComponent<TextRendererComponent>().SortingLayer = v;
					}),
			});

		DeclareConflict<SpriteRendererComponent, TextRendererComponent>(sceneManager);
		DeclareConflict<ImageComponent, TextRendererComponent>(sceneManager);
		DeclareConflict<ParticleSystem2DComponent, TextRendererComponent>(sceneManager);

		// ── Physics (Box2D-backed) ──────────────────────────────────

		RegisterComponent<Rigidbody2DComponent>(sceneManager, "Rigidbody 2D",
			ComponentCategory::Component, "Physics", "Rigidbody2D",
			{
				Properties::MakeWith<Vec2>("Position", "Position",
					[](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().GetPosition(); },
					[](Entity& e, const Vec2& v) {
						e.GetComponent<Rigidbody2DComponent>().SetPosition(v);
					},
					[]() { PropertyMetadata m; m.ReadOnly = true; return m; }()),
				Properties::MakeWith<Vec2>("Velocity", "Velocity",
					[](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().GetVelocity(); },
					[](Entity& e, const Vec2& v) {
						e.GetComponent<Rigidbody2DComponent>().SetVelocity(v);
					}),
				Properties::MakeWith<float>("Rotation", "Rotation",
					[](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().GetRotation(); },
					[](Entity& e, float v) {
						e.GetComponent<Rigidbody2DComponent>().SetRotation(v);
					},
					[]() { PropertyMetadata m; m.DragSpeed = 0.01f; m.ReadOnly = true; return m; }()),
				Properties::MakeWith<float>("GravityScale", "Gravity Scale",
					[](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().GetGravityScale(); },
					[](Entity& e, float v) {
						e.GetComponent<Rigidbody2DComponent>().SetGravityScale(v);
					},
					[]() { PropertyMetadata m; m.HasClamp = true; m.ClampMin = 0.0; m.ClampMax = 1.0; return m; }()),
				Properties::MakeWith<BodyType>("BodyType", "Body Type",
					[](const Entity& e) {
						return e.GetComponent<Rigidbody2DComponent>().GetBodyType();
					},
					[](Entity& e, BodyType v) {
						e.GetComponent<Rigidbody2DComponent>().SetBodyType(v);
					}),
			});

		RegisterComponent<BoxCollider2DComponent>(sceneManager, "Box Collider 2D",
			ComponentCategory::Component, "Physics", "BoxCollider2D",
			{
				Properties::MakeWith<Vec2>("Offset", "Offset",
					[](const Entity& e) { return e.GetComponent<BoxCollider2DComponent>().GetCenter(); },
					[](Entity& e, const Vec2& v) {
						if (Scene* s = e.GetScene()) {
							e.GetComponent<BoxCollider2DComponent>().SetCenter(v, *s);
						}
					},
					[]() { PropertyMetadata m; m.DragSpeed = 0.05f; return m; }()),
				Properties::MakeWith<Vec2>("Size", "Size",
					[](const Entity& e) {
						const Scene* s = e.GetScene();
						if (!s) return Vec2{ 1.0f, 1.0f };
						return e.GetComponent<BoxCollider2DComponent>().GetLocalScale(*s);
					},
					[](Entity& e, const Vec2& localSize) {
						Scene* s = e.GetScene();
						if (!s) return;
						const Vec2 clamped{ std::max(localSize.x, 0.001f), std::max(localSize.y, 0.001f) };
						e.GetComponent<BoxCollider2DComponent>().SetScale(clamped, *s);
					},
					[]() { PropertyMetadata m; m.HasClamp = true; m.ClampMin = 0.001; m.ClampMax = 1000.0; m.DragSpeed = 0.05f; return m; }()),
				Properties::MakeWith<bool>("Sensor", "Sensor",
					[](const Entity& e) { return e.GetComponent<BoxCollider2DComponent>().IsSensor(); },
					[](Entity& e, bool v) {
						auto& col = e.GetComponent<BoxCollider2DComponent>();
						if (Scene* s = e.GetScene()) col.SetSensor(v, *s);
					}),
				Properties::MakeWith<bool>("ContactEvents", "Contact Events",
					[](const Entity& e) { return e.GetComponent<BoxCollider2DComponent>().CanRegisterContacts(); },
					[](Entity& e, bool v) {
						e.GetComponent<BoxCollider2DComponent>().SetRegisterContacts(v);
					}),
				Properties::MakeWith<bool>("Enabled", "Collider Enabled",
					[](const Entity& e) { return e.GetComponent<BoxCollider2DComponent>().IsEnabled(); },
					[](Entity& e, bool v) {
						e.GetComponent<BoxCollider2DComponent>().SetEnabled(v);
					}),
				Properties::MakeWith<float>("Friction", "Friction",
					[](const Entity& e) { return e.GetComponent<BoxCollider2DComponent>().GetFriction(); },
					[](Entity& e, float v) {
						e.GetComponent<BoxCollider2DComponent>().SetFriction(v);
					},
					[]() { PropertyMetadata m; m.HasClamp = true; m.ClampMin = 0.0; m.ClampMax = 10.0; m.DragSpeed = 0.01f; return m; }()),
				Properties::MakeWith<float>("Bounciness", "Bounciness",
					[](const Entity& e) { return e.GetComponent<BoxCollider2DComponent>().GetBounciness(); },
					[](Entity& e, float v) {
						e.GetComponent<BoxCollider2DComponent>().SetBounciness(v);
					},
					[]() { PropertyMetadata m; m.HasClamp = true; m.ClampMin = 0.0; m.ClampMax = 1.0; m.DragSpeed = 0.01f; return m; }()),
				Properties::MakeWith<uint64_t>("LayerMask", "Layer Mask",
					[](const Entity& e) { return e.GetComponent<BoxCollider2DComponent>().GetLayer(); },
					[](Entity& e, uint64_t v) {
						e.GetComponent<BoxCollider2DComponent>().SetLayer(v);
					}),
			});

		// ── Physics (Axiom-Physics, lightweight AABB) ──────────────

		RegisterComponent<FastBody2DComponent>(sceneManager, "Fast Body 2D",
			ComponentCategory::Component, "Physics", "FastBody2D",
			{
				Properties::MakeWith<AxiomPhys::BodyType>("Type", "Body Type",
					[](const Entity& e) { return e.GetComponent<FastBody2DComponent>().Type; },
					[](Entity& e, AxiomPhys::BodyType v) {
						auto& body = e.GetComponent<FastBody2DComponent>();
						body.Type = v;
						if (body.m_Body) body.m_Body->SetBodyType(v);
					}),
				Properties::MakeWith<float>("Mass", "Mass",
					[](const Entity& e) { return e.GetComponent<FastBody2DComponent>().Mass; },
					[](Entity& e, float v) {
						auto& body = e.GetComponent<FastBody2DComponent>();
						body.Mass = v;
						if (body.m_Body) body.m_Body->SetMass(v);
					},
					[]() { PropertyMetadata m; m.HasClamp = true; m.ClampMin = 0.001; m.ClampMax = 10000.0; m.DragSpeed = 0.1f; return m; }()),
				Properties::MakeWith<bool>("UseGravity", "Use Gravity",
					[](const Entity& e) { return e.GetComponent<FastBody2DComponent>().UseGravity; },
					[](Entity& e, bool v) {
						auto& body = e.GetComponent<FastBody2DComponent>();
						body.UseGravity = v;
						if (body.m_Body) body.m_Body->SetGravityEnabled(v);
					}),
				Properties::MakeWith<bool>("BoundaryCheck", "Boundary Check",
					[](const Entity& e) { return e.GetComponent<FastBody2DComponent>().BoundaryCheck; },
					[](Entity& e, bool v) {
						auto& body = e.GetComponent<FastBody2DComponent>();
						body.BoundaryCheck = v;
						if (body.m_Body) body.m_Body->SetBoundaryCheckEnabled(v);
					}),
			});

		RegisterComponent<FastBoxCollider2DComponent>(sceneManager, "Fast Box Collider 2D",
			ComponentCategory::Component, "Physics", "FastBoxCollider2D",
			{
				Properties::MakeWith<Vec2>("HalfExtents", "Half Extents",
					[](const Entity& e) { return e.GetComponent<FastBoxCollider2DComponent>().HalfExtents; },
					[](Entity& e, const Vec2& v) {
						e.GetComponent<FastBoxCollider2DComponent>().SetHalfExtents(v);
					},
					[]() { PropertyMetadata m; m.DragSpeed = 0.05f; return m; }()),
			});

		RegisterComponent<FastCircleCollider2DComponent>(sceneManager, "Fast Circle Collider 2D",
			ComponentCategory::Component, "Physics", "FastCircleCollider2D",
			{
				Properties::MakeWith<float>("Radius", "Radius",
					[](const Entity& e) { return e.GetComponent<FastCircleCollider2DComponent>().Radius; },
					[](Entity& e, float v) {
						e.GetComponent<FastCircleCollider2DComponent>().SetRadius(v);
					},
					[]() { PropertyMetadata m; m.HasClamp = true; m.ClampMin = 0.01; m.ClampMax = 100.0; m.DragSpeed = 0.05f; return m; }()),
			});

		// ── Audio ───────────────────────────────────────────────────

		RegisterComponent<AudioSourceComponent>(sceneManager, "Audio Source",
			ComponentCategory::Component, "Audio", "AudioSource",
			{
				Properties::MakeWith<bool>("PlayOnAwake", "Play On Awake",
					[](const Entity& e) { return e.GetComponent<AudioSourceComponent>().GetPlayOnAwake(); },
					[](Entity& e, bool v) {
						e.GetComponent<AudioSourceComponent>().SetPlayOnAwake(v);
					}),
				Properties::MakeWith<float>("Volume", "Volume",
					[](const Entity& e) { return e.GetComponent<AudioSourceComponent>().GetVolume(); },
					[](Entity& e, float v) {
						e.GetComponent<AudioSourceComponent>().SetVolume(v);
					},
					[]() { PropertyMetadata m; m.HasClamp = true; m.ClampMin = 0.0; m.ClampMax = 1.0; m.DragSpeed = 0.01f; return m; }()),
				Properties::MakeWith<float>("Pitch", "Pitch",
					[](const Entity& e) { return e.GetComponent<AudioSourceComponent>().GetPitch(); },
					[](Entity& e, float v) {
						e.GetComponent<AudioSourceComponent>().SetPitch(v);
					},
					[]() { PropertyMetadata m; m.HasClamp = true; m.ClampMin = 0.1; m.ClampMax = 3.0; m.DragSpeed = 0.01f; return m; }()),
				Properties::MakeWith<bool>("Loop", "Loop",
					[](const Entity& e) { return e.GetComponent<AudioSourceComponent>().IsLooping(); },
					[](Entity& e, bool v) {
						e.GetComponent<AudioSourceComponent>().SetLoop(v);
					}),
				Properties::MakeAudioRef("Clip", "Clip",
					[](const Entity& e) -> uint64_t {
						const auto& src = e.GetComponent<AudioSourceComponent>();
						const AudioHandle h = src.GetAudioHandle();
						return h.IsValid() ? AudioManager::GetAudioAssetUUID(h) : 0ull;
					},
					[](Entity& e, uint64_t uuid) {
						auto& src = e.GetComponent<AudioSourceComponent>();
						const AudioHandle h = uuid != 0
							? AudioManager::LoadAudioByUUID(uuid)
							: AudioHandle{};
						src.SetAudioHandle(h, UUID(uuid));
					}),
			});

		// ── UI widgets ──────────────────────────────────────────────
		// All show up in the inspector's "UI" tab. The Interactable
		// component is the input-state primitive; Button/Slider/etc.
		// are visual presets that read it. They have no inspector
		// fields beyond what the struct exposes — defaults are sane.

		RegisterComponent<UIInteractableComponent>(sceneManager, "UI Interactable",
			ComponentCategory::Component, "UI", "UIInteractable",
			{
				Properties::Make("Interactable", "Interactable", &UIInteractableComponent::Interactable),
			});

		RegisterComponent<UIButtonComponent>(sceneManager, "UI Button",
			ComponentCategory::Component, "UI", "UIButton",
			{
				Properties::Make("NormalColor",   "Normal Color",   &UIButtonComponent::NormalColor),
				Properties::Make("HoveredColor",  "Hovered Color",  &UIButtonComponent::HoveredColor),
				Properties::Make("PressedColor",  "Pressed Color",  &UIButtonComponent::PressedColor),
				Properties::Make("DisabledColor", "Disabled Color", &UIButtonComponent::DisabledColor),
			});

		RegisterComponent<UISliderComponent>(sceneManager, "UI Slider",
			ComponentCategory::Component, "UI", "UISlider",
			{
				Properties::MakeWith<float>("Value", "Value",
					[](const Entity& e) { return e.GetComponent<UISliderComponent>().Value; },
					[](Entity& e, float v) { e.GetComponent<UISliderComponent>().Value = v; },
					[]() { PropertyMetadata m; m.DragSpeed = 0.01f; return m; }()),
				Properties::Make("MinValue",     "Min Value",     &UISliderComponent::MinValue),
				Properties::Make("MaxValue",     "Max Value",     &UISliderComponent::MaxValue),
				Properties::Make("WholeNumbers", "Whole Numbers", &UISliderComponent::WholeNumbers),
			});

		RegisterComponent<UIInputFieldComponent>(sceneManager, "UI Input Field",
			ComponentCategory::Component, "UI", "UIInputField",
			{
				Properties::Make("Text",            "Text",            &UIInputFieldComponent::Text),
				Properties::Make("PlaceholderText", "Placeholder",     &UIInputFieldComponent::PlaceholderText),
				Properties::Make("CharacterLimit",  "Character Limit", &UIInputFieldComponent::CharacterLimit),
			});

		RegisterComponent<UIDropdownComponent>(sceneManager, "UI Dropdown",
			ComponentCategory::Component, "UI", "UIDropdown",
			{
				Properties::Make("SelectedIndex", "Selected Index", &UIDropdownComponent::SelectedIndex),
			});

		RegisterComponent<UIToggleComponent>(sceneManager, "UI Toggle",
			ComponentCategory::Component, "UI", "UIToggle",
			{
				Properties::Make("IsOn", "Is On", &UIToggleComponent::IsOn),
			});

		// HierarchyComponent is real but managed via Entity::SetParent;
		// users don't add it from the Add Component popup. Tagged so the
		// inspector hides it from the picker.
		RegisterComponent<HierarchyComponent>(sceneManager, "Hierarchy", ComponentCategory::Tag);

		// ── Conflicts ───────────────────────────────────────────────
		// One renderer per entity (Tilemap2D adds its own conflicts in its package init).
		DeclareConflict<SpriteRendererComponent, ParticleSystem2DComponent>(sceneManager);
		DeclareConflict<SpriteRendererComponent, ImageComponent>(sceneManager);
		DeclareConflict<ImageComponent, ParticleSystem2DComponent>(sceneManager);

		// Box2D and Axiom-Physics stacks must not be mixed per-entity.
		DeclareConflict<Rigidbody2DComponent, FastBody2DComponent>(sceneManager);
		DeclareConflict<BoxCollider2DComponent, FastBoxCollider2DComponent>(sceneManager);
		DeclareConflict<BoxCollider2DComponent, FastCircleCollider2DComponent>(sceneManager);
		// Within Axiom-Physics, only one shape per body is supported.
		DeclareConflict<FastBoxCollider2DComponent, FastCircleCollider2DComponent>(sceneManager);

		// Tags (not user-addable)
		RegisterComponent<IdTag>(sceneManager, "Id", ComponentCategory::Tag);
		RegisterComponent<StaticTag>(sceneManager, "Static", ComponentCategory::Tag);
		RegisterComponent<DisabledTag>(sceneManager, "Disabled", ComponentCategory::Tag);
		RegisterComponent<DeadlyTag>(sceneManager, "Deadly", ComponentCategory::Tag);
	}
}

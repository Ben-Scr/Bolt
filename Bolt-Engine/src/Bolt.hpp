#pragma once

// Bolt.hpp is the minimal public umbrella for the core contract only.
// Explicit compatibility path: define BOLT_ALL_MODULES before including this header
// to restore the legacy wide umbrella for full-module consumers.

// Collections
#include "Collections/Color.hpp"
#include "Collections/Mat2.hpp"
#include "Collections/Vec2.hpp"
#include "Collections/Vec4.hpp"
#include "Collections/Viewport.hpp"

// Core
#include "Core/ApplicationConfig.hpp"
#include "Core/Assert.hpp"
#include "Core/Base.hpp"
#include "Core/Exceptions.hpp"
#include "Core/Export.hpp"
#include "Core/Log.hpp"
#include "Core/Time.hpp"
#include "Core/Version.hpp"

#if defined(BOLT_ALL_MODULES)
    // Components
    #include "Components/Components.hpp"

    // Core module entry points
    #include "Core/Application.hpp"
    #include "Core/Input.hpp"
    #include "Core/KeyCodes.hpp"

    // Graphics
    #include "Graphics/Gizmo.hpp"
    #include "Graphics/Texture2D.hpp"
    #include "Graphics/TextureHandle.hpp"
    #include "Graphics/TextureManager.hpp"

    // Math
    #include "Math/Common.hpp"

    // Physics
    #include "Physics/Box2DWorld.hpp"
    #include "Physics/Collision2D.hpp"
    #include "Physics/Physics2D.hpp"
    #include "Physics/PhysicsUtility.hpp"

    // Audio
    #include "Audio/Audio.hpp"
    #include "Audio/AudioHandle.hpp"
    #include "Audio/AudioManager.hpp"

    // Scene
    #include "Scene/Entity.hpp"
    #include "Scene/EntityHandle.hpp"
    #include "Scene/Scene.hpp"
    #include "Scene/SceneManager.hpp"

    // Events
    #include "Events/Events.hpp"

    // Utils
    #include "Utils/StringHelper.hpp"
#endif

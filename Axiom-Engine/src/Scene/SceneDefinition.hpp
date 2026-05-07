#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <typeindex>
#include <algorithm>
#include <tuple>
#include <type_traits>
#include "Core/Export.hpp"
#include "Core/Log.hpp"

namespace Axiom {
    class Scene;
    class ISystem;

    class AXIOM_API SceneDefinition {
        friend class Scene;
        friend class SceneManager;

    public:
        explicit SceneDefinition(const std::string& name)
            : m_Name(name) {
        }


        template<typename TSystem, typename... Args>
        SceneDefinition& AddSystem(Args&&... args) {
            static_assert(std::is_base_of<ISystem, TSystem>::value,
                "TSystem must derive from ISystem");

            const std::type_index systemType = typeid(TSystem);
            if (std::find(m_SystemTypes.begin(), m_SystemTypes.end(), systemType) != m_SystemTypes.end()) {
                AIM_CORE_WARN_TAG("SceneDefinition", "System '{}' is already registered on scene '{}'", systemType.name(), m_Name);
                return *this;
            }

            using CapturedArgs = std::tuple<std::decay_t<Args>...>;
            m_SystemFactories.emplace_back(
                [capturedArgs = CapturedArgs(std::forward<Args>(args)...)]() mutable {
                    return std::apply(
                        [](auto&&... a) {
                            return std::make_unique<TSystem>(std::forward<decltype(a)>(a)...);
                        },
                        capturedArgs
                    );
                }
            );


            m_SystemTypes.push_back(systemType);

            return *this;
        }


        SceneDefinition& OnInitialize(std::function<void(Scene&)> callback) {
            m_InitializeCallbacks.push_back(callback);
            return *this;
        }


        SceneDefinition& OnLoad(std::function<void(Scene&)> callback) {
            m_LoadCallbacks.push_back(callback);
            return *this;
        }

        SceneDefinition& OnUnload(std::function<void(Scene&)> callback) {
            m_UnloadCallbacks.push_back(callback);
            return *this;
        }


        SceneDefinition& SetAsStartupScene() {
            m_IsStartupScene = true;
            return *this;
        }


        SceneDefinition& SetPersistent(bool persistent) {
            m_IsPersistent = persistent;
            return *this;
        }

        const std::string& GetName() const { return m_Name; }
        bool IsStartupScene() const { return m_IsStartupScene; }
        bool IsPersistent() const { return m_IsPersistent; }

    private:
        std::string m_Name;
        std::vector<std::function<std::unique_ptr<ISystem>()>> m_SystemFactories;
        std::vector<std::type_index> m_SystemTypes;

        std::vector<std::function<void(Scene&)>> m_InitializeCallbacks;
        std::vector<std::function<void(Scene&)>> m_LoadCallbacks;
        std::vector<std::function<void(Scene&)>> m_UnloadCallbacks;

        bool m_IsStartupScene = false;
        bool m_IsPersistent = false;

        std::shared_ptr<Scene> Instantiate() const;
    };
}

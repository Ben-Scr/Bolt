#pragma once
#include "Scene/ComponentInfo.hpp"

#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace Axiom {
    class ComponentRegistry {
    public:
        template<typename T>
        void Register(ComponentInfo info) {
            const std::type_index id = typeid(T);
            const auto existing = m_map.find(id);
            if (existing != m_map.end()) {
                if (info.serializedName.empty()) info.serializedName = existing->second.serializedName;
                if (!info.drawInspector) info.drawInspector = existing->second.drawInspector;
            }

            info.has = [](Entity e) { return e.HasComponent<T>(); };
            info.add = [](Entity e) { e.AddComponent<T>(); };
            info.remove = [](Entity e) { e.RemoveComponent<T>(); };

            if constexpr (!std::is_empty_v<T>) {
                info.copyTo = [](Entity src, Entity dst) {
                    if (!src.HasComponent<T>()) return;
                    if (dst.HasComponent<T>())
                        dst.GetComponent<T>() = src.GetComponent<T>();
                    else
                        dst.AddComponent<T>(src.GetComponent<T>());
                };
            } else {
                info.copyTo = [](Entity src, Entity dst) {
                    if (src.HasComponent<T>() && !dst.HasComponent<T>())
                        dst.AddComponent<T>();
                };
            }

            m_map[id] = std::move(info);
        }

        const auto& All() const { return m_map; }

        template <typename F>
        void ForEachComponentInfo(F&& fn) {
            for (auto& [id, info] : m_map)
                fn(id, info);
        }

        template <typename F>
        void ForEachComponentInfo(F&& fn) const {
            for (const auto& [id, info] : m_map)
                fn(id, info);
        }

        void CopyComponents(Entity src, Entity dst) const {
            for (const auto& [id, info] : m_map) {
                (void)id;
                if (info.copyTo) {
                    info.copyTo(src, dst);
                }
            }
        }

    private:
        std::unordered_map<std::type_index, ComponentInfo> m_map;
    };
}

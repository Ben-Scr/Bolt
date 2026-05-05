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
                if (info.properties.empty()) info.properties = existing->second.properties;
                if (info.conflictsWith.empty()) info.conflictsWith = existing->second.conflictsWith;
                // Preserve serialize/deserialize: AttachInspector re-registers and would otherwise drop them.
                if (!info.serialize) info.serialize = existing->second.serialize;
                if (!info.deserialize) info.deserialize = existing->second.deserialize;
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

        /// Resolve ComponentInfo for hybrid inspectors that mix DrawAll with custom widgets.
        template <typename T>
        const ComponentInfo* GetInfo() const {
            const auto it = m_map.find(typeid(T));
            return it != m_map.end() ? &it->second : nullptr;
        }

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

        /// Bidirectional conflict check; either side's declaration counts.
        bool HasConflict(Entity entity, std::type_index proposed) const {
            const auto proposedIt = m_map.find(proposed);
            const ComponentInfo* proposedInfo = (proposedIt != m_map.end()) ? &proposedIt->second : nullptr;

            for (const auto& [existingId, existingInfo] : m_map) {
                if (existingId == proposed) continue;
                if (!existingInfo.has || !existingInfo.has(entity)) continue;

                // proposed → existing
                if (proposedInfo) {
                    for (const std::type_index& conflict : proposedInfo->conflictsWith) {
                        if (conflict == existingId) return true;
                    }
                }
                // existing → proposed
                for (const std::type_index& conflict : existingInfo.conflictsWith) {
                    if (conflict == proposed) return true;
                }
            }
            return false;
        }

        /// Type-pair check for callers that already have type_index values.
        bool TypesConflict(std::type_index a, std::type_index b) const {
            if (a == b) return false;
            const auto ai = m_map.find(a);
            if (ai != m_map.end()) {
                for (const std::type_index& c : ai->second.conflictsWith) {
                    if (c == b) return true;
                }
            }
            const auto bi = m_map.find(b);
            if (bi != m_map.end()) {
                for (const std::type_index& c : bi->second.conflictsWith) {
                    if (c == a) return true;
                }
            }
            return false;
        }

    private:
        std::unordered_map<std::type_index, ComponentInfo> m_map;
    };
}

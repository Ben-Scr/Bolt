#pragma once
#include <functional>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <mutex>
#include <utility>
#include "Collections/Ids.hpp"

namespace Bolt {
    template<typename... Args>
    class Event {
    public:
        using Callback = std::function<void(Args...)>;

        EventId Add(Callback cb) {
            std::scoped_lock lock(m_Mutex);
            const EventId id = EventId(++m_NextId.value);
            m_Listeners.push_back({ id, std::move(cb) });
            return id;
        }
        bool Remove(EventId id) {
            std::scoped_lock lock(m_Mutex);
            auto it = std::remove_if(m_Listeners.begin(), m_Listeners.end(), [id](const Entry& e) { return e.id == id; });
            const bool removed = (it != m_Listeners.end());
            m_Listeners.erase(it, m_Listeners.end());
            return removed;
        }

        void Clear() {
            std::scoped_lock lock(m_Mutex);
            m_Listeners.clear();
        }

        bool HasListeners() const {
            std::scoped_lock lock(m_Mutex);
            return !m_Listeners.empty();
        }

        void Invoke(Args... args) {
            std::vector<Entry> listenersSnapshot;
            {
                std::scoped_lock lock(m_Mutex);
                listenersSnapshot = m_Listeners;
            }
            for (const auto& e : listenersSnapshot) {
                e.cb(args...);
            }
        }


    private:
        struct Entry {
            EventId id;
            Callback cb;
        };

        mutable std::mutex m_Mutex;
        std::vector<Entry> m_Listeners;
        EventId m_NextId;
    };
}

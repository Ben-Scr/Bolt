#pragma once
#include <functional>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include "Collections/Ids.hpp"

namespace Axiom {
    template<typename... Args>
    class Event {
    public:
        using Callback = std::function<void(Args...)>;

        EventId Add(Callback cb) {
            std::unique_lock lock(m_Mutex);
            const EventId id = EventId(++m_NextId.value);
            m_Listeners.push_back({ id, std::move(cb) });
            return id;
        }

        // Remove blocks until any in-flight Invoke on another thread has completed, so a
        // subscriber's destructor can safely call Remove and trust that no further
        // callbacks will fire against `this`. Without this, the previous snapshot-based
        // dispatch could call into a destroyed subscriber (the std::function copy in
        // the snapshot kept the captured `this` past Remove). Use a shared_mutex:
        // Invoke takes a shared lock so multiple dispatchers can run concurrently;
        // Remove takes an exclusive lock so it waits for all dispatchers to release.
        bool Remove(EventId id) {
            std::unique_lock lock(m_Mutex);
            auto it = std::remove_if(m_Listeners.begin(), m_Listeners.end(), [id](const Entry& e) { return e.id == id; });
            const bool removed = (it != m_Listeners.end());
            m_Listeners.erase(it, m_Listeners.end());
            return removed;
        }

        void Clear() {
            std::unique_lock lock(m_Mutex);
            m_Listeners.clear();
        }

        bool HasListeners() const {
            std::shared_lock lock(m_Mutex);
            return !m_Listeners.empty();
        }

        void Invoke(Args... args) {
            // Hold the shared lock across dispatch so a concurrent Remove (which takes
            // the unique lock) blocks until we're done. Callers therefore must NOT
            // Add/Remove from inside their own callback — that would deadlock. The
            // engine's existing usage (Log::OnLog, etc.) all subscribes/unsubscribes
            // from outside the callback path, so this restriction is acceptable.
            std::shared_lock lock(m_Mutex);
            for (const auto& e : m_Listeners) {
                e.cb(args...);
            }
        }


    private:
        struct Entry {
            EventId id;
            Callback cb;
        };

        mutable std::shared_mutex m_Mutex;
        std::vector<Entry> m_Listeners;
        EventId m_NextId;
    };
}

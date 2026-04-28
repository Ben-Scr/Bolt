#pragma once
#include "Collections/Ids.hpp"
#include "Core/Export.hpp"
#include "Events/BoltEvent.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace Bolt {
	class Application;

	class BOLT_API EventBus {
	public:
		using Callback = std::function<void(BoltEvent&)>;

		class Subscription {
		public:
			Subscription() = default;

			Subscription(EventBus& bus, EventId id)
				: m_Bus(&bus), m_Id(id)
			{
			}

			Subscription(const Subscription&) = delete;
			Subscription& operator=(const Subscription&) = delete;

			Subscription(Subscription&& other) noexcept
			{
				MoveFrom(other);
			}

			Subscription& operator=(Subscription&& other) noexcept
			{
				if (this != &other)
				{
					Reset();
					MoveFrom(other);
				}

				return *this;
			}

			~Subscription()
			{
				Reset();
			}

			bool Reset()
			{
				if (!m_Bus)
					return false;

				EventBus* bus = m_Bus;
				const EventId id = m_Id;
				m_Bus = nullptr;
				m_Id = {};
				return bus->Unsubscribe(id);
			}

			EventId Release()
			{
				const EventId id = m_Id;
				m_Bus = nullptr;
				m_Id = {};
				return id;
			}

			EventId GetId() const { return m_Id; }
			bool IsSubscribed() const { return m_Bus != nullptr; }
			explicit operator bool() const { return IsSubscribed(); }

		private:
			void MoveFrom(Subscription& other) noexcept
			{
				m_Bus = other.m_Bus;
				m_Id = other.m_Id;
				other.m_Bus = nullptr;
				other.m_Id = {};
			}

			EventBus* m_Bus = nullptr;
			EventId m_Id{};
		};

		EventId Subscribe(Callback callback) {
			const EventId id(++m_NextId.value);
			m_Listeners.push_back({ id, std::move(callback), true });
			return id;
		}

		template<typename TEvent, typename F>
		EventId Subscribe(F&& callback) {
			return Subscribe([fn = std::forward<F>(callback)](BoltEvent& event) mutable {
				if (event.GetEventType() == TEvent::GetStaticType()) {
					fn(static_cast<TEvent&>(event));
				}
			});
		}

		Subscription SubscribeScoped(Callback callback) {
			return Subscription(*this, Subscribe(std::move(callback)));
		}

		template<typename TEvent, typename F>
		Subscription SubscribeScoped(F&& callback) {
			return Subscription(*this, Subscribe<TEvent>(std::forward<F>(callback)));
		}

		bool Unsubscribe(EventId id) {
			if (m_PublishDepth > 0) {
				for (Entry& entry : m_Listeners) {
					if (entry.Id == id && entry.Active) {
						entry.Active = false;
						m_NeedsCompact = true;
						return true;
					}
				}

				return false;
			}

			auto it = std::remove_if(m_Listeners.begin(), m_Listeners.end(), [id](const Entry& entry) {
				return entry.Id == id;
			});
			const bool removed = it != m_Listeners.end();
			m_Listeners.erase(it, m_Listeners.end());
			return removed;
		}

		void Clear() {
			if (m_PublishDepth > 0) {
				for (Entry& entry : m_Listeners) {
					entry.Active = false;
				}
				m_NeedsCompact = true;
				return;
			}

			m_Listeners.clear();
		}

	private:
		friend class Application;

		struct Entry {
			EventId Id;
			Callback Listener;
			bool Active = true;
		};

		void Publish(BoltEvent& event) {
			++m_PublishDepth;
			try {
				const size_t listenerCount = m_Listeners.size();
				for (size_t i = 0; i < listenerCount && i < m_Listeners.size(); ++i) {
					if (event.Handled) {
						break;
					}

					if (!m_Listeners[i].Active) {
						continue;
					}

					Callback listener = m_Listeners[i].Listener;
					listener(event);
				}
			}
			catch (...) {
				FinishPublish();
				throw;
			}

			FinishPublish();
		}

		void FinishPublish() {
			if (m_PublishDepth == 0) {
				return;
			}

			--m_PublishDepth;

			if (m_PublishDepth == 0 && m_NeedsCompact) {
				CompactInactive();
			}
		}

		void CompactInactive() {
			auto it = std::remove_if(m_Listeners.begin(), m_Listeners.end(), [](const Entry& entry) {
				return !entry.Active;
			});
			m_Listeners.erase(it, m_Listeners.end());
			m_NeedsCompact = false;
		}

		std::vector<Entry> m_Listeners;
		EventId m_NextId{};
		uint32_t m_PublishDepth = 0;
		bool m_NeedsCompact = false;
	};
}

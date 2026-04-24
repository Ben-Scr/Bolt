#pragma once
#include "Collections/Ids.hpp"
#include "Core/Export.hpp"
#include "Events/BoltEvent.hpp"

#include <algorithm>
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
			m_Listeners.push_back({ id, std::move(callback) });
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
			auto it = std::remove_if(m_Listeners.begin(), m_Listeners.end(), [id](const Entry& entry) {
				return entry.Id == id;
			});
			const bool removed = it != m_Listeners.end();
			m_Listeners.erase(it, m_Listeners.end());
			return removed;
		}

		void Clear() {
			m_Listeners.clear();
		}

	private:
		friend class Application;

		struct Entry {
			EventId Id;
			Callback Listener;
		};

		void Publish(BoltEvent& event) {
			const auto snapshot = m_Listeners;
			for (const Entry& entry : snapshot) {
				if (event.Handled) {
					break;
				}
				entry.Listener(event);
			}
		}

		std::vector<Entry> m_Listeners;
		EventId m_NextId{};
	};
}

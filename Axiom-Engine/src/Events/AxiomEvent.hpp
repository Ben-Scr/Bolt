#pragma once
#include "Events/EventTypes.hpp"

#include <functional>
#include <string>

namespace Axiom {

#define AIM_EVENT_CLASS_TYPE(type)                                           \
	static EventType GetStaticType() { return EventType::type; }            \
	EventType GetEventType() const override { return GetStaticType(); }     \
	const char* GetName() const override { return #type; }

#define AIM_EVENT_CLASS_CATEGORY(category)                                   \
	int GetCategoryFlags() const override { return category; }

	class AxiomEvent {
	public:
		bool Handled = false;

		virtual ~AxiomEvent() = default;
		virtual EventType GetEventType() const = 0;
		virtual const char* GetName() const = 0;
		virtual int GetCategoryFlags() const = 0;
		virtual std::string ToString() const { return GetName(); }

		bool IsInCategory(EventCategory category) const {
			return (GetCategoryFlags() & category) != 0;
		}
	};

	using EventCallbackFn = std::function<void(AxiomEvent&)>;

} // namespace Axiom

#include "Events/EventDispatcher.hpp"

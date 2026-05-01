#pragma once

#include "Events/AxiomEvent.hpp"

namespace Axiom {

	class WindowResizeEvent : public AxiomEvent {
	public:
		WindowResizeEvent(int width, int height)
			: m_Width(width), m_Height(height) {
		}

		int GetWidth() const { return m_Width; }
		int GetHeight() const { return m_Height; }

		std::string ToString() const override {
			return std::string("WindowResizeEvent: ") + std::to_string(m_Width) + ", " + std::to_string(m_Height);
		}

		AIM_EVENT_CLASS_TYPE(WindowResize)
		AIM_EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		int m_Width;
		int m_Height;
	};

} // namespace Axiom

#pragma once

#include "Core/Log.hpp"
#include "Utils/Timer.hpp"

#include <sstream>
#include <string>

namespace Axiom {

	class ScopedTimer
	{
	public:
		ScopedTimer(const std::string& name, const std::string& description)
			: m_Name(name), m_Description(description) {
		}
		~ScopedTimer()
		{
			float time = m_Timer.ElapsedMilliseconds();
			std::ostringstream oss;
			oss << m_Description << " - " << time << "ms";
			AIM_INFO_TAG(m_Name, oss.str());
		}

	private:
		std::string m_Name;
		std::string m_Description;
		Timer m_Timer;
	};

} // namespace Axiom

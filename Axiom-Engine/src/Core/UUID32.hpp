#pragma once

#include "Base.hpp"

namespace Axiom {

	class UUID32
	{
	public:
		UUID32();
		UUID32(uint32_t uuid);
		UUID32(const UUID32& other);

		operator uint32_t () { return m_UUID; }
		operator const uint32_t() const { return m_UUID; }

	private:
		uint32_t m_UUID;
	};

}

namespace std {

	template <>
	struct hash<Axiom::UUID32>
	{
		std::size_t operator()(const Axiom::UUID32& uuid) const
		{
			return hash<uint32_t>()((uint32_t)uuid);
		}
	};

}
#pragma once

#include <xhash>

namespace aveng {

	class UUID {
	
	public:

		UUID();
		UUID(uint64_t uuid);
		UUID(const UUID&) = default;

		operator uint64_t() const { return m_UUID; }

	private:
		uint64_t m_UUID;

	};

}

namespace std {

	// Template specialization for us to be able to hash an aveng::UUID
	template<>
	struct hash<aveng::UUID>
	{
		// Specialized call operator, redirected to a typical call to hash()
		std::size_t operator()(const aveng::UUID& uuid) const
		{
			return hash<uint64_t>()((uint64_t)uuid);
		}

	};

}
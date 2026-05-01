#include "pch.hpp"
#include "Random.hpp"
#include "Collections/Color.hpp"
#include <limits>

namespace Axiom {
	Color Random::NextColor() {
		return Color(NextFloat(0.f, 1.f), NextFloat(0.f, 1.f), NextFloat(0.f, 1.f));
	}
	bool Random::NextBool() {
		static thread_local std::bernoulli_distribution dist(0.5);
		std::scoped_lock lock(s_Mutex);
		return dist(s_Gen);
	}
	std::uint8_t Random::NextByte(std::uint8_t max) {
		static thread_local std::uniform_int_distribution<int> dist;
		std::scoped_lock lock(s_Mutex);
		return dist(s_Gen, typename decltype(dist)::param_type(0, max));
	}
	std::uint8_t Random::NextByte(std::uint8_t min, std::uint8_t max) {
		if (min > max) { AIM_CORE_WARN_TAG("Random", "min > max, swapping"); std::swap(min, max); }

		static thread_local std::uniform_int_distribution<int> dist;
		std::scoped_lock lock(s_Mutex);
		return dist(s_Gen, typename decltype(dist)::param_type(min, max));
	}

	double Random::NextDouble() {
		static thread_local std::uniform_real_distribution<double> dist(0.0, 1.f);
		std::scoped_lock lock(s_Mutex);
		return dist(s_Gen);
	}
	double Random::NextDouble(double max) {
		if (max < 0.0) { AIM_CORE_WARN_TAG("Random", "Negative max clamped to 0"); max = 0.0; }

		static thread_local std::uniform_real_distribution<double> dist;
		std::scoped_lock lock(s_Mutex);
		return dist(s_Gen, typename decltype(dist)::param_type(0, max));
	}
	double Random::NextDouble(double min, double max) {
		if (min > max) { AIM_CORE_WARN_TAG("Random", "min > max, swapping"); std::swap(min, max); }

		static thread_local std::uniform_real_distribution<double> dist;
		std::scoped_lock lock(s_Mutex);
		return dist(s_Gen, typename decltype(dist)::param_type(min, max));
	}

	float Random::NextFloat() {
		static thread_local std::uniform_real_distribution<float> dist(0.f, 1.f);
		std::scoped_lock lock(s_Mutex);
		return dist(s_Gen);
	}
	float Random::NextFloat(float max) {
		if (max < 0.0f) { AIM_CORE_WARN_TAG("Random", "Negative max clamped to 0"); max = 0.0f; }

		static thread_local std::uniform_real_distribution<float> dist;
		std::scoped_lock lock(s_Mutex);
		return dist(s_Gen, typename decltype(dist)::param_type(0.f, max));
	}
	float Random::NextFloat(float min, float max) {
		if (min > max) { AIM_CORE_WARN_TAG("Random", "min > max, swapping"); std::swap(min, max); }

		static thread_local std::uniform_real_distribution<float> dist;
		std::scoped_lock lock(s_Mutex);
		return dist(s_Gen, typename decltype(dist)::param_type(min, max));
	}

	int Random::NextInt() {
		static thread_local std::uniform_int_distribution<int> dist(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
		std::scoped_lock lock(s_Mutex);
		return dist(s_Gen);
	}
	int Random::NextInt(int max) {
		if (max < 0) { AIM_CORE_WARN_TAG("Random", "Negative max clamped to 0"); max = 0; }

		static thread_local std::uniform_int_distribution<int> dist;
		std::scoped_lock lock(s_Mutex);
		return dist(s_Gen, typename decltype(dist)::param_type(0, max));
	}
	int Random::NextInt(int min, int max) {
		if (min > max) { AIM_CORE_WARN_TAG("Random", "min > max, swapping"); std::swap(min, max); }

		static thread_local std::uniform_int_distribution<int> dist;
		std::scoped_lock lock(s_Mutex);
		return dist(s_Gen, typename decltype(dist)::param_type(min, max));
	}
}
#pragma once

//#include "Ref.h"

#include <magic_enum/magic_enum.hpp>

#include <functional>
#include  <memory>

namespace Axiom {

	void InitializeCore();
	void ShutdownCore();

}

#if !defined(AIM_PLATFORM_WINDOWS) && !defined(AIM_PLATFORM_LINUX)
#error Unknown platform.
#endif

#define BIT(x) (1u << x)

#if defined(__GNUC__)
#if defined(__clang__)
#define AIM_COMPILER_CLANG
#else
#define AIM_COMPILER_GCC
#endif
#elif defined(_MSC_VER)
#define AIM_COMPILER_MSVC
#endif

#define AIM_BIND_EVENT_FN(fn) std::bind(&fn, this, std::placeholders::_1)

#ifdef AIM_COMPILER_MSVC
#define AIM_FORCE_INLINE __forceinline
#define AIM_EXPLICIT_STATIC static
#elif defined(__GNUC__)
#define AIM_FORCE_INLINE __attribute__((always_inline)) inline
#define AIM_EXPLICIT_STATIC
#else
#define AIM_FORCE_INLINE inline
#define AIM_EXPLICIT_STATIC
#endif

#define AIM_STRINGIFY_IMPL(x) #x
#define AIM_STRINGIFY(x) AIM_STRINGIFY_IMPL(x)

namespace Axiom {
	template<typename T>
	T RoundDown(T x, T fac) { return x / fac * fac; }

	template<typename T>
	T RoundUp(T x, T fac) { return RoundDown(x + fac - 1, fac); }

	// Pointer wrappers
	template<typename T>
	using Scope = std::unique_ptr<T>;
	template<typename T, typename ... Args>
	constexpr Scope<T> CreateScope(Args&& ... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	using byte = uint8_t;

	/** A simple wrapper for std::atomic_flag to avoid confusing
		function names usage. The object owning it can still be
		default copyable, but the copied flag is going to be reset.
	*/
	struct AtomicFlag
	{
		AIM_FORCE_INLINE void SetDirty() { flag.clear(); }
		AIM_FORCE_INLINE bool CheckAndResetIfDirty() { return !flag.test_and_set(); }

		explicit AtomicFlag() noexcept { flag.test_and_set(); }
		AtomicFlag(const AtomicFlag&) noexcept {}
		AtomicFlag& operator=(const AtomicFlag&) noexcept { return *this; }
		AtomicFlag(AtomicFlag&&) noexcept {};
		AtomicFlag& operator=(AtomicFlag&&) noexcept { return *this; }

	private:
		std::atomic_flag flag;
	};

	struct Flag
	{
		AIM_FORCE_INLINE void SetDirty() noexcept { flag = true; }
		AIM_FORCE_INLINE bool CheckAndResetIfDirty() noexcept
		{
			if (flag)
				return !(flag = !flag);
			else
				return false;
		}

		AIM_FORCE_INLINE bool IsDirty() const noexcept { return flag; }

	private:
		bool flag = false;
	};

	//==============================================================================
	/// Convinient interface for enum based bit flag options
	template<typename TEnum> requires std::is_enum_v<TEnum>
	struct Flags
	{
		using EnumType = TEnum;
		using UnderlyingType = magic_enum::underlying_type_t<EnumType>;

		constexpr Flags() = default;
		constexpr Flags(UnderlyingType value) : m_Flags(value) {}

		constexpr void Set(UnderlyingType flag) { m_Flags |= flag; }
		constexpr void Unset(UnderlyingType flag) { m_Flags &= (~flag); }
		constexpr Flags<EnumType> With(UnderlyingType flag) { m_Flags |= flag; return *this; }
		constexpr Flags<EnumType> Without(UnderlyingType flag) { m_Flags &= ~flag; return *this; }
		constexpr bool IsSet(EnumType flag) const { return (m_Flags & flag); }
		constexpr bool AnySet() const { return static_cast<bool>(m_Flags); }
		constexpr UnderlyingType GetValue() const { return m_Flags; }
		constexpr void Clear() { m_Flags = UnderlyingType(0); }

		constexpr bool operator==(const Flags<EnumType>&) const = default;

	private:
		UnderlyingType m_Flags = 0;
	};
}
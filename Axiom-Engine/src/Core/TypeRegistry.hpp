#pragma once

#include "MetaHelpers.hpp"

#include <tuple>
#include <optional>
#include <array>

namespace Axiom
{
	template<class ...TTypes>
	class TypeRegistry
	{
		TypeRegistry() = delete;
	private:
		struct InvocationDefault
		{
			template<class TLambda, class TTuple>
			static constexpr auto InvokeLambda(TLambda&& lambda, TTuple&& args)
			{
				(std::apply([&](auto&&...largs)
					{
						lambda.template operator() < TTypes > (std::forward<decltype(largs)>(largs)...);
					}, args),
					...);
			}
		};

		template<class ...TExcludes>
		struct InvocationExclude
		{
			template<class T>
			static constexpr bool is_excluded_v = std::disjunction_v<std::is_same<T, TExcludes>... >;

			template<class TLambda, class TTuple>
			static constexpr auto InvokeLambda(TLambda&& lambda, TTuple&& args)
			{
				(std::apply([&](auto&&...largs)
					{
						if constexpr (!is_excluded_v<TTypes>)
							lambda.template operator() < TTypes > (std::forward<decltype(largs)>(largs)...);
					}, args),
					...);
			}
		};

	public:
		template<class TLambda, class ...TArgs>
		static constexpr void InvokeOnRegisteredTypes(TLambda&& lambda, TArgs&&...args)
		{
			InvocationDefault::InvokeLambda(std::forward<TLambda>(lambda), std::forward_as_tuple(std::forward<TArgs>(args)...));
		};

		template<class ...TAdditionalTypes, class TLambda, class ...TArgs>
		static constexpr void InvokeOnRegisteredTypesAnd(TLambda&& lambda, TArgs&&...args)
		{
			TypeRegistry<TTypes..., TAdditionalTypes...>::InvokeOnRegisteredTypes(std::forward<TLambda>(lambda), std::forward_as_tuple(std::forward<TArgs>(args)...));
		};

		template<class ...TExcludeTypes, class TLambda, class ...TArgs>
		static constexpr void InvokeOnRegisteredTypesExcluding(TLambda&& lambda, TArgs&&...args)
		{
			InvocationExclude<TExcludeTypes...>::InvokeLambda(std::forward<TLambda>(lambda), std::forward_as_tuple(std::forward<TArgs>(args)...));
		};
	};

	template<typename TValueType, typename ...Types>
	struct TypeMap
	{
	public:
		using ValueType = TValueType;

		template<typename ...KeyTypes>
		static constexpr auto Of()
		{
			return TypeMap<ValueType, KeyTypes...>();
		}

		template<typename Type>
		constexpr std::optional<ValueType> Find()
		{
			constexpr size_t index = meta::Meta_find_index<TypeList, Type>::value;
			static_assert(index != meta::Meta_npos);

			return m_Values[index];
		}

		template<typename Type>
		constexpr void Set(ValueType value)
		{
			constexpr size_t index = meta::Meta_find_index<TypeList, Type>::value;
			static_assert(index != meta::Meta_npos);

			m_Values[index] = std::move(value);
		}

		template<typename Type>
		constexpr bool Erase()
		{
			constexpr size_t index = meta::Meta_find_index<TypeList, Type>::value;
			static_assert(index != meta::Meta_npos);

			if (m_Values[index])
			{
				m_Values[index] = std::nullopt;
				return true;
			}

			return false;
		}

	private:
		using TypeList = meta::Meta_list<Types...>;

		static constexpr size_t Count = sizeof...(Types);
		std::array<std::optional<ValueType>, Count> m_Values;
	};

}
#pragma once

#include "Core/Export.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Axiom::Json {

	class AXIOM_API Value {
	public:
		enum class Type {
			Null,
			Bool,
			Number,
			String,
			Object,
			Array
		};

		using Object = std::vector<std::pair<std::string, Value>>;
		using Array = std::vector<Value>;

		Value() = default;
		Value(std::nullptr_t);
		Value(bool value);
		Value(double value);
		Value(int value);
		Value(int64_t value);
		Value(uint64_t value);
		Value(const char* value);
		Value(std::string value);

		static Value MakeObject();
		static Value MakeArray();

		Type GetType() const { return m_Type; }
		bool IsNull() const { return m_Type == Type::Null; }
		bool IsBool() const { return m_Type == Type::Bool; }
		bool IsNumber() const { return m_Type == Type::Number; }
		bool IsString() const { return m_Type == Type::String; }
		bool IsObject() const { return m_Type == Type::Object; }
		bool IsArray() const { return m_Type == Type::Array; }

		bool AsBoolOr(bool fallback) const;
		double AsDoubleOr(double fallback) const;
		int AsIntOr(int fallback) const;
		int64_t AsInt64Or(int64_t fallback) const;
		uint64_t AsUInt64Or(uint64_t fallback) const;
		std::string AsStringOr(std::string fallback = {}) const;

		Object& GetObject();
		const Object& GetObject() const;
		Array& GetArray();
		const Array& GetArray() const;

		Value* FindMember(std::string_view key);
		const Value* FindMember(std::string_view key) const;

		Value& AddMember(std::string key, Value value);
		Value& Append(Value value);

	private:
		void SetType(Type type);

	private:
		Type m_Type = Type::Null;
		bool m_Bool = false;
		double m_Number = 0.0;
		std::string m_String;
		Object m_Object;
		Array m_Array;
	};

	AXIOM_API bool TryParse(std::string_view text, Value& outValue, std::string* outError = nullptr);
	AXIOM_API Value Parse(std::string_view text, std::string* outError = nullptr);
	AXIOM_API std::string EscapeString(std::string_view value);
	AXIOM_API std::string Stringify(const Value& value, bool pretty = false, int indentSize = 2);

}

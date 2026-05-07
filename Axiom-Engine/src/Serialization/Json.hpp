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

		// Internal representation kind for Number values. Public IsNumber()
		// stays true for all three so existing callers work unchanged. The
		// distinction only matters when round-tripping integers larger than
		// 2^53 (UUIDs / AssetGUIDs) — storing those as `double` loses
		// precision; the integer kinds preserve the exact value.
		enum class NumberKind {
			Double,
			Int64,
			UInt64
		};

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
		NumberKind GetNumberKind() const { return m_NumberKind; }
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

		// Read-only accessors. Return a static empty container if the value
		// holds the wrong kind so callers don't accidentally retype the value
		// just by reading it. Use EnsureObject/EnsureArray when you intend to
		// mutate the value into that kind.
		Object& GetObject();
		const Object& GetObject() const;
		Array& GetArray();
		const Array& GetArray() const;

		// Make-or-get mutators: convert this value into Object/Array kind (if
		// it isn't already) and return a mutable reference. This is what the
		// non-const GetObject/GetArray used to do; that silent retyping caused
		// data loss when callers happened to reach them in a non-mutating
		// context.
		Object& EnsureObject();
		Array& EnsureArray();

		Value* FindMember(std::string_view key);
		const Value* FindMember(std::string_view key) const;

		Value& AddMember(std::string key, Value value);
		Value& Append(Value value);

		// Structural equality (Int64(5) == UInt64(5) == Double(5.0)).
		AXIOM_API friend bool operator==(const Value& a, const Value& b);
		friend bool operator!=(const Value& a, const Value& b) { return !(a == b); }

	private:
		void SetType(Type type);

	private:
		Type m_Type = Type::Null;
		NumberKind m_NumberKind = NumberKind::Double;
		bool m_Bool = false;
		double m_Number = 0.0;
		int64_t m_Int64 = 0;
		uint64_t m_UInt64 = 0;
		std::string m_String;
		Object m_Object;
		Array m_Array;
	};

	AXIOM_API bool TryParse(std::string_view text, Value& outValue, std::string* outError = nullptr);
	AXIOM_API Value Parse(std::string_view text, std::string* outError = nullptr);
	AXIOM_API std::string EscapeString(std::string_view value);
	AXIOM_API std::string Stringify(const Value& value, bool pretty = false, int indentSize = 2);

}

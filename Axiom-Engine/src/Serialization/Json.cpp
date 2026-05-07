#include "pch.hpp"
#include "Serialization/Json.hpp"
#include "Core/Log.hpp"

#include <cctype>
#include <cstdlib>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>

namespace Axiom::Json {

	namespace {
		// Recursion cap rejects pathological deep nesting before it overflows the parser stack.
		constexpr int kMaxParseDepth = 256;

		class Parser {
		public:
			explicit Parser(std::string_view text)
				: m_Text(text) {
			}

			bool ParseValue(Value& outValue, std::string* outError) {
				// Skip leading UTF-8 BOM (EF BB BF) — some editors emit it though it's not valid JSON.
				if (m_Text.size() >= 3
					&& static_cast<unsigned char>(m_Text[0]) == 0xEF
					&& static_cast<unsigned char>(m_Text[1]) == 0xBB
					&& static_cast<unsigned char>(m_Text[2]) == 0xBF) {
					m_Position = 3;
				}

				SkipWhitespace();
				if (!ParseAny(outValue, outError, 0)) {
					return false;
				}

				SkipWhitespace();
				if (!IsAtEnd()) {
					SetError(outError, "Unexpected trailing characters");
					return false;
				}

				return true;
			}

		private:
			bool ParseAny(Value& outValue, std::string* outError, int depth) {
				if (depth >= kMaxParseDepth) {
					SetError(outError, "JSON nesting depth exceeded");
					return false;
				}

				SkipWhitespace();
				if (IsAtEnd()) {
					SetError(outError, "Unexpected end of JSON input");
					return false;
				}

				switch (Peek()) {
				case 'n':
					return ParseLiteral("null", Value(), outValue, outError);
				case 't':
					return ParseLiteral("true", Value(true), outValue, outError);
				case 'f':
					return ParseLiteral("false", Value(false), outValue, outError);
				case '"':
					return ParseStringValue(outValue, outError);
				case '{':
					return ParseObject(outValue, outError, depth);
				case '[':
					return ParseArray(outValue, outError, depth);
				default:
					if (Peek() == '-' || std::isdigit(static_cast<unsigned char>(Peek()))) {
						return ParseNumber(outValue, outError);
					}

					SetError(outError, "Unexpected token while parsing JSON");
					return false;
				}
			}

			bool ParseLiteral(std::string_view literal, Value literalValue, Value& outValue, std::string* outError) {
				if (m_Text.substr(m_Position, literal.size()) != literal) {
					SetError(outError, "Invalid JSON literal");
					return false;
				}

				m_Position += literal.size();
				outValue = std::move(literalValue);
				return true;
			}

			bool ParseStringValue(Value& outValue, std::string* outError) {
				std::string parsed;
				if (!ParseString(parsed, outError)) {
					return false;
				}

				outValue = Value(std::move(parsed));
				return true;
			}

			bool ParseString(std::string& outValue, std::string* outError) {
				if (!Consume('"')) {
					SetError(outError, "Expected opening quote");
					return false;
				}

				std::string result;
				while (!IsAtEnd()) {
					char ch = Advance();
					if (ch == '"') {
						outValue = std::move(result);
						return true;
					}

					if (ch != '\\') {
						result.push_back(ch);
						continue;
					}

					if (IsAtEnd()) {
						SetError(outError, "Invalid escape sequence");
						return false;
					}

					char escaped = Advance();
					switch (escaped) {
					case '"': result.push_back('"'); break;
					case '\\': result.push_back('\\'); break;
					case '/': result.push_back('/'); break;
					case 'b': result.push_back('\b'); break;
					case 'f': result.push_back('\f'); break;
					case 'n': result.push_back('\n'); break;
					case 'r': result.push_back('\r'); break;
					case 't': result.push_back('\t'); break;
					case 'u':
					{
						uint32_t codePoint = 0;
						if (!ParseUnicodeEscape(codePoint, outError)) {
							return false;
						}
						// Combine UTF-16 surrogate pair (high D800-DBFF + low DC00-DFFF) into one code point.
						if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
							if (m_Position + 2 > m_Text.size()
								|| m_Text[m_Position] != '\\'
								|| m_Text[m_Position + 1] != 'u') {
								SetError(outError, "Lone high surrogate in unicode escape");
								return false;
							}
							m_Position += 2; // consume '\u'
							uint32_t low = 0;
							if (!ParseUnicodeEscape(low, outError)) {
								return false;
							}
							if (low < 0xDC00 || low > 0xDFFF) {
								SetError(outError, "Invalid low surrogate in unicode escape");
								return false;
							}
							codePoint = 0x10000u
								+ ((codePoint - 0xD800u) << 10)
								+ (low - 0xDC00u);
						}
						else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
							SetError(outError, "Lone low surrogate in unicode escape");
							return false;
						}
						AppendUtf8(result, codePoint);
						break;
					}
					default:
						SetError(outError, "Unsupported escape sequence");
						return false;
					}
				}

				SetError(outError, "Unterminated JSON string");
				return false;
			}

			bool ParseUnicodeEscape(uint32_t& outCodePoint, std::string* outError) {
				if (m_Position + 4 > m_Text.size()) {
					SetError(outError, "Incomplete unicode escape");
					return false;
				}

				uint32_t codePoint = 0;
				for (int i = 0; i < 4; i++) {
					char digit = m_Text[m_Position++];
					codePoint <<= 4;
					if (digit >= '0' && digit <= '9') {
						codePoint |= static_cast<uint32_t>(digit - '0');
					}
					else if (digit >= 'a' && digit <= 'f') {
						codePoint |= static_cast<uint32_t>(digit - 'a' + 10);
					}
					else if (digit >= 'A' && digit <= 'F') {
						codePoint |= static_cast<uint32_t>(digit - 'A' + 10);
					}
					else {
						SetError(outError, "Invalid unicode escape");
						return false;
					}
				}

				outCodePoint = codePoint;
				return true;
			}

			static void AppendUtf8(std::string& out, uint32_t codePoint) {
				if (codePoint <= 0x7F) {
					out.push_back(static_cast<char>(codePoint));
				}
				else if (codePoint <= 0x7FF) {
					out.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
					out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
				}
				else if (codePoint <= 0xFFFF) {
					out.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
					out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
				}
				else {
					out.push_back(static_cast<char>(0xF0 | ((codePoint >> 18) & 0x07)));
					out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
				}
			}

			bool ParseNumber(Value& outValue, std::string* outError) {
				const size_t start = m_Position;
				bool isNegative = false;

				if (Peek() == '-') {
					isNegative = true;
					Advance();
				}

				if (IsAtEnd()) {
					SetError(outError, "Invalid JSON number");
					return false;
				}

				if (Peek() == '0') {
					Advance();
					// Reject leading zeros (`01`, `09`) per JSON spec — a `0`
					// can only be followed by `.`, exponent, or end-of-number.
					if (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
						SetError(outError, "Invalid JSON number: leading zero");
						return false;
					}
				}
				else if (std::isdigit(static_cast<unsigned char>(Peek()))) {
					while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
						Advance();
					}
				}
				else {
					SetError(outError, "Invalid JSON number");
					return false;
				}

				bool isFloat = false;
				if (!IsAtEnd() && Peek() == '.') {
					isFloat = true;
					Advance();
					if (IsAtEnd() || !std::isdigit(static_cast<unsigned char>(Peek()))) {
						SetError(outError, "Invalid JSON number fraction");
						return false;
					}
					while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
						Advance();
					}
				}

				if (!IsAtEnd() && (Peek() == 'e' || Peek() == 'E')) {
					isFloat = true;
					Advance();
					if (!IsAtEnd() && (Peek() == '+' || Peek() == '-')) {
						Advance();
					}
					if (IsAtEnd() || !std::isdigit(static_cast<unsigned char>(Peek()))) {
						SetError(outError, "Invalid JSON exponent");
						return false;
					}
					while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
						Advance();
					}
				}

				const std::string numericText(m_Text.substr(start, m_Position - start));

				// Integers go to int64/uint64 to preserve full precision (UUIDs are >2^53).
				if (!isFloat) {
					if (isNegative) {
						const char* first = numericText.data();
						const char* last = first + numericText.size();
						int64_t intValue = 0;
						auto [ptr, ec] = std::from_chars(first, last, intValue);
						if (ec == std::errc{} && ptr == last) {
							outValue = Value(intValue);
							return true;
						}
						// Out of int64 range — fall through to double.
					}
					else {
						const char* first = numericText.data();
						const char* last = first + numericText.size();
						uint64_t uintValue = 0;
						auto [ptr, ec] = std::from_chars(first, last, uintValue);
						if (ec == std::errc{} && ptr == last) {
							outValue = Value(uintValue);
							return true;
						}
						// Out of uint64 range — fall through to double.
					}
				}

				// Use std::from_chars for locale-independent parsing — std::strtod honors LC_NUMERIC,
				// so a German locale would treat "3.14" as invalid (expects "3,14"), corrupting any
				// JSON read on those systems.
				const char* first = numericText.data();
				const char* last = first + numericText.size();
				double parsed = 0.0;
				auto [ptr, ec] = std::from_chars(first, last, parsed);
				if (ec != std::errc{} || ptr != last) {
					SetError(outError, "Failed to parse JSON number");
					return false;
				}

				outValue = Value(parsed);
				return true;
			}

			bool ParseObject(Value& outValue, std::string* outError, int depth) {
				if (!Consume('{')) {
					SetError(outError, "Expected object start");
					return false;
				}

				Value objectValue = Value::MakeObject();
				SkipWhitespace();
				if (Consume('}')) {
					outValue = std::move(objectValue);
					return true;
				}

				while (!IsAtEnd()) {
					std::string key;
					if (!ParseString(key, outError)) {
						return false;
					}

					SkipWhitespace();
					if (!Consume(':')) {
						SetError(outError, "Expected ':' after object key");
						return false;
					}

					Value childValue;
					if (!ParseAny(childValue, outError, depth + 1)) {
						return false;
					}

					objectValue.AddMember(std::move(key), std::move(childValue));

					SkipWhitespace();
					if (Consume('}')) {
						outValue = std::move(objectValue);
						return true;
					}

					if (!Consume(',')) {
						SetError(outError, "Expected ',' between object members");
						return false;
					}

					SkipWhitespace();
				}

				SetError(outError, "Unterminated JSON object");
				return false;
			}

			bool ParseArray(Value& outValue, std::string* outError, int depth) {
				if (!Consume('[')) {
					SetError(outError, "Expected array start");
					return false;
				}

				Value arrayValue = Value::MakeArray();
				SkipWhitespace();
				if (Consume(']')) {
					outValue = std::move(arrayValue);
					return true;
				}

				while (!IsAtEnd()) {
					Value childValue;
					if (!ParseAny(childValue, outError, depth + 1)) {
						return false;
					}

					arrayValue.Append(std::move(childValue));

					SkipWhitespace();
					if (Consume(']')) {
						outValue = std::move(arrayValue);
						return true;
					}

					if (!Consume(',')) {
						SetError(outError, "Expected ',' between array items");
						return false;
					}

					SkipWhitespace();
				}

				SetError(outError, "Unterminated JSON array");
				return false;
			}

			void SkipWhitespace() {
				while (!IsAtEnd() && std::isspace(static_cast<unsigned char>(m_Text[m_Position]))) {
					m_Position++;
				}
			}

			bool IsAtEnd() const {
				return m_Position >= m_Text.size();
			}

			char Peek() const {
				return IsAtEnd() ? '\0' : m_Text[m_Position];
			}

			char Advance() {
				return IsAtEnd() ? '\0' : m_Text[m_Position++];
			}

			bool Consume(char expected) {
				if (Peek() != expected) {
					return false;
				}
				m_Position++;
				return true;
			}

			void SetError(std::string* outError, std::string_view message) const {
				if (!outError) {
					return;
				}

				std::ostringstream stream;
				stream << message << " at byte " << m_Position;
				*outError = stream.str();
			}

		private:
			std::string_view m_Text;
			size_t m_Position = 0;
		};

		void WriteIndent(std::string& out, int depth, int indentSize) {
			out.append(static_cast<size_t>(depth * indentSize), ' ');
		}

		void WriteValue(std::string& out, const Value& value, bool pretty, int depth, int indentSize) {
			switch (value.GetType()) {
			case Value::Type::Null:
				out += "null";
				break;
			case Value::Type::Bool:
				out += value.AsBoolOr(false) ? "true" : "false";
				break;
			case Value::Type::Number:
			{
				// Integer kinds emit without a decimal point so they round-trip
				// losslessly. The Double kind uses precision-15 stream output.
				switch (value.GetNumberKind()) {
				case Value::NumberKind::Int64:
					out += std::to_string(value.AsInt64Or(0));
					break;
				case Value::NumberKind::UInt64:
					out += std::to_string(value.AsUInt64Or(0));
					break;
				case Value::NumberKind::Double:
				default:
				{
					// to_chars on stack buffer; ostringstream alloc dominated Stringify time on large scenes.
					char buffer[32];
					const double doubleValue = value.AsDoubleOr(0.0);
					auto [ptr, ec] = std::to_chars(
						buffer, buffer + sizeof(buffer), doubleValue,
						std::chars_format::general);
					if (ec == std::errc{}) {
						out.append(buffer, static_cast<size_t>(ptr - buffer));
					}
					else {
						// Unreachable for finite doubles in 32 bytes; fall back rather than emit garbage.
						out += "0";
					}
					break;
				}
				}
				break;
			}
			case Value::Type::String:
				out += '"';
				out += EscapeString(value.AsStringOr());
				out += '"';
				break;
			case Value::Type::Object:
			{
				out += '{';
				const auto& members = value.GetObject();
				if (!members.empty()) {
					if (pretty) {
						out += '\n';
					}

					for (size_t i = 0; i < members.size(); i++) {
						if (pretty) {
							WriteIndent(out, depth + 1, indentSize);
						}
						out += '"';
						out += EscapeString(members[i].first);
						out += '"';
						out += pretty ? ": " : ":";
						WriteValue(out, members[i].second, pretty, depth + 1, indentSize);
						if (i + 1 < members.size()) {
							out += ',';
						}
						if (pretty) {
							out += '\n';
						}
					}

					if (pretty) {
						WriteIndent(out, depth, indentSize);
					}
				}
				out += '}';
				break;
			}
			case Value::Type::Array:
			{
				out += '[';
				const auto& items = value.GetArray();
				if (!items.empty()) {
					if (pretty) {
						out += '\n';
					}

					for (size_t i = 0; i < items.size(); i++) {
						if (pretty) {
							WriteIndent(out, depth + 1, indentSize);
						}
						WriteValue(out, items[i], pretty, depth + 1, indentSize);
						if (i + 1 < items.size()) {
							out += ',';
						}
						if (pretty) {
							out += '\n';
						}
					}

					if (pretty) {
						WriteIndent(out, depth, indentSize);
					}
				}
				out += ']';
				break;
			}
			}
		}
	} // namespace

	Value::Value(std::nullptr_t) {
	}

	Value::Value(bool value)
		: m_Type(Type::Bool)
		, m_Bool(value) {
	}

	Value::Value(double value)
		: m_Type(Type::Number)
		, m_NumberKind(NumberKind::Double)
		, m_Number(value) {
	}

	Value::Value(int value)
		: Value(static_cast<int64_t>(value)) {
	}

	Value::Value(int64_t value)
		: m_Type(Type::Number)
		, m_NumberKind(NumberKind::Int64)
		, m_Number(static_cast<double>(value))
		, m_Int64(value) {
	}

	Value::Value(uint64_t value)
		: m_Type(Type::Number)
		, m_NumberKind(NumberKind::UInt64)
		, m_Number(static_cast<double>(value))
		, m_UInt64(value) {
	}

	Value::Value(const char* value)
		: Value(value ? std::string(value) : std::string()) {
	}

	Value::Value(std::string value)
		: m_Type(Type::String)
		, m_String(std::move(value)) {
	}

	Value Value::MakeObject() {
		Value value;
		value.SetType(Type::Object);
		return value;
	}

	Value Value::MakeArray() {
		Value value;
		value.SetType(Type::Array);
		return value;
	}

	bool Value::AsBoolOr(bool fallback) const {
		return IsBool() ? m_Bool : fallback;
	}

	double Value::AsDoubleOr(double fallback) const {
		if (!IsNumber()) return fallback;
		switch (m_NumberKind) {
		case NumberKind::Int64:  return static_cast<double>(m_Int64);
		case NumberKind::UInt64: return static_cast<double>(m_UInt64);
		case NumberKind::Double:
		default:                 return m_Number;
		}
	}

	int Value::AsIntOr(int fallback) const {
		if (!IsNumber()) {
			return fallback;
		}

		if (m_NumberKind == NumberKind::Int64) {
			if (m_Int64 < std::numeric_limits<int>::min() ||
			    m_Int64 > std::numeric_limits<int>::max()) {
				return fallback;
			}
			return static_cast<int>(m_Int64);
		}
		if (m_NumberKind == NumberKind::UInt64) {
			if (m_UInt64 > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
				return fallback;
			}
			return static_cast<int>(m_UInt64);
		}

		// double(INT_MAX) rounds up; comparing >= INT_MAX+1.0 avoids the UB-on-cast hazard.
		if (m_Number < static_cast<double>(std::numeric_limits<int>::min()) ||
			m_Number >= static_cast<double>(std::numeric_limits<int>::max()) + 1.0) {
			return fallback;
		}

		return static_cast<int>(m_Number);
	}

	int64_t Value::AsInt64Or(int64_t fallback) const {
		if (!IsNumber()) {
			return fallback;
		}

		if (m_NumberKind == NumberKind::Int64) {
			return m_Int64;
		}
		if (m_NumberKind == NumberKind::UInt64) {
			if (m_UInt64 > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
				return fallback;
			}
			return static_cast<int64_t>(m_UInt64);
		}

		// double(INT64_MAX) rounds up; use next-lower exactly-representable double to avoid UB on cast.
		constexpr double k_MaxSafeI64 = 9223372036854774784.0;  // INT64_MAX rounded down
		constexpr double k_MinSafeI64 = -9223372036854775808.0; // INT64_MIN is exactly representable

		if (m_Number < k_MinSafeI64 || m_Number > k_MaxSafeI64) {
			return fallback;
		}

		return static_cast<int64_t>(m_Number);
	}

	uint64_t Value::AsUInt64Or(uint64_t fallback) const {
		if (!IsNumber()) {
			return fallback;
		}

		if (m_NumberKind == NumberKind::UInt64) {
			return m_UInt64;
		}
		if (m_NumberKind == NumberKind::Int64) {
			if (m_Int64 < 0) return fallback;
			return static_cast<uint64_t>(m_Int64);
		}

		// Same UB hazard as AsInt64Or — use next-lower exactly-representable double.
		constexpr double k_MaxSafeU64 = 18446744073709549568.0;

		if (m_Number < 0.0 || m_Number > k_MaxSafeU64) {
			return fallback;
		}

		return static_cast<uint64_t>(m_Number);
	}

	std::string Value::AsStringOr(std::string fallback) const {
		return IsString() ? m_String : std::move(fallback);
	}

	Value::Object& Value::GetObject() {
		// Non-const read used to silently retype the value via SetType, which
		// nuked any previously-stored payload. Now match the const overload's
		// behavior and return a static empty so a stray read doesn't lose data.
		static Object emptyObject;
		if (!IsObject()) {
			AIM_CORE_WARN_TAG("Serialization", "Json::GetObject called on non-object value; returning empty container. Use EnsureObject to convert.");
			emptyObject.clear();
			return emptyObject;
		}
		return m_Object;
	}

	const Value::Object& Value::GetObject() const {
		static const Object emptyObject;
		return IsObject() ? m_Object : emptyObject;
	}

	Value::Array& Value::GetArray() {
		// See GetObject — same trap on the array side.
		static Array emptyArray;
		if (!IsArray()) {
			AIM_CORE_WARN_TAG("Serialization", "Json::GetArray called on non-array value; returning empty container. Use EnsureArray to convert.");
			emptyArray.clear();
			return emptyArray;
		}
		return m_Array;
	}

	const Value::Array& Value::GetArray() const {
		static const Array emptyArray;
		return IsArray() ? m_Array : emptyArray;
	}

	Value::Object& Value::EnsureObject() {
		// Explicit make-or-get path. Callers that want to convert the value
		// into Object kind opt-in via this name so the conversion can't happen
		// by accident.
		SetType(Type::Object);
		return m_Object;
	}

	Value::Array& Value::EnsureArray() {
		SetType(Type::Array);
		return m_Array;
	}

	Value* Value::FindMember(std::string_view key) {
		if (!IsObject()) {
			return nullptr;
		}

		for (auto& [memberKey, memberValue] : m_Object) {
			if (memberKey == key) {
				return &memberValue;
			}
		}

		return nullptr;
	}

	const Value* Value::FindMember(std::string_view key) const {
		if (!IsObject()) {
			return nullptr;
		}

		for (const auto& [memberKey, memberValue] : m_Object) {
			if (memberKey == key) {
				return &memberValue;
			}
		}

		return nullptr;
	}

	Value& Value::AddMember(std::string key, Value value) {
		SetType(Type::Object);
		for (auto& [memberKey, memberValue] : m_Object) {
			if (memberKey == key) {
				// Overwrite-on-duplicate is load-bearing (BuildOverridePatch re-keys); trace for visibility.
				AIM_CORE_TRACE_TAG("Serialization",
					"Duplicate JSON key '{}' overwritten", key);
				memberValue = std::move(value);
				return memberValue;
			}
		}

		m_Object.emplace_back(std::move(key), std::move(value));
		return m_Object.back().second;
	}

	Value& Value::Append(Value value) {
		SetType(Type::Array);
		m_Array.emplace_back(std::move(value));
		return m_Array.back();
	}

	void Value::SetType(Type type) {
		if (m_Type == type) {
			return;
		}

		m_Type = type;
		m_NumberKind = NumberKind::Double;
		m_Bool = false;
		m_Number = 0.0;
		m_Int64 = 0;
		m_UInt64 = 0;
		m_String.clear();
		m_Object.clear();
		m_Array.clear();
	}

	bool operator==(const Value& a, const Value& b) {
		if (a.m_Type != b.m_Type) return false;
		switch (a.m_Type) {
		case Value::Type::Null: return true;
		case Value::Type::Bool: return a.m_Bool == b.m_Bool;
		case Value::Type::Number: {
			auto numericValue = [](const Value& v) -> double {
				switch (v.m_NumberKind) {
				case Value::NumberKind::Int64:  return static_cast<double>(v.m_Int64);
				case Value::NumberKind::UInt64: return static_cast<double>(v.m_UInt64);
				case Value::NumberKind::Double: return v.m_Number;
				}
				return v.m_Number;
			};
			if (a.m_NumberKind == b.m_NumberKind) {
				switch (a.m_NumberKind) {
				case Value::NumberKind::Int64:  return a.m_Int64 == b.m_Int64;
				case Value::NumberKind::UInt64: return a.m_UInt64 == b.m_UInt64;
				case Value::NumberKind::Double: return a.m_Number == b.m_Number;
				}
			}
			return numericValue(a) == numericValue(b);
		}
		case Value::Type::String: return a.m_String == b.m_String;
		case Value::Type::Array: {
			if (a.m_Array.size() != b.m_Array.size()) return false;
			for (size_t i = 0; i < a.m_Array.size(); ++i) {
				if (!(a.m_Array[i] == b.m_Array[i])) return false;
			}
			return true;
		}
		case Value::Type::Object: {
			if (a.m_Object.size() != b.m_Object.size()) return false;
			for (const auto& [key, value] : a.m_Object) {
				bool found = false;
				for (const auto& [bKey, bValue] : b.m_Object) {
					if (bKey == key) {
						if (!(value == bValue)) return false;
						found = true;
						break;
					}
				}
				if (!found) return false;
			}
			return true;
		}
		}
		return false;
	}

	bool TryParse(std::string_view text, Value& outValue, std::string* outError) {
		Parser parser(text);
		return parser.ParseValue(outValue, outError);
	}

	Value Parse(std::string_view text, std::string* outError) {
		Value parsed;
		TryParse(text, parsed, outError);
		return parsed;
	}

	std::string EscapeString(std::string_view value) {
		std::string escaped;
		escaped.reserve(value.size());

		for (const char ch : value) {
			switch (ch) {
			case '"': escaped += "\\\""; break;
			case '\\': escaped += "\\\\"; break;
			case '\b': escaped += "\\b"; break;
			case '\f': escaped += "\\f"; break;
			case '\n': escaped += "\\n"; break;
			case '\r': escaped += "\\r"; break;
			case '\t': escaped += "\\t"; break;
			default:
				if (static_cast<unsigned char>(ch) < 0x20) {
					std::ostringstream stream;
					stream << "\\u"
					       << std::hex
					       << std::uppercase
					       << static_cast<int>(static_cast<unsigned char>(ch) + 0x10000);
					std::string unicode = stream.str();
					escaped += "\\u";
					escaped.append(unicode.end() - 4, unicode.end());
				}
				else {
					escaped.push_back(ch);
				}
				break;
			}
		}

		return escaped;
	}

	std::string Stringify(const Value& value, bool pretty, int indentSize) {
		std::string output;
		WriteValue(output, value, pretty, 0, indentSize);
		if (pretty) {
			output += '\n';
		}
		return output;
	}

}
